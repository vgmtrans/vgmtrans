/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace vgmtrans::formats::akao {

// FF7's key-on and lookahead rules (SCUS_941.63: 80030e7c, 800318bc).
// Bytecode, note timing, instruments, and ADSR remain in the shared decoder.
class AkaoFF7Voice {
public:
  enum class Mode { Normal, Slur, Legato };

  void start(Mode mode) {
    settings_.mode = sinceNote_.mode = mode;
    continues_ = false;  // Every CC/D0/DA starts with a fresh attack.
  }

  void portamento(u16 duration) {
    start(Mode::Slur);
    settings_.portamentoTicks = sinceNote_.portamentoTicks = duration;
    previousKey_.reset();
  }

  void end(core::PerformanceEmitter& out) {
    // Native lookahead clears modes before the preceding timed event. Apply
    // its gate/pitch correction here, following the VM's actual repeat path.
    // Preserve settings established since that event: they follow lookahead.
    settings_ = sinceNote_;
    continues_ = false;
    if (auto gate = std::exchange(last_.shortGateEnd, std::nullopt)) {
      out.setNoteEnd(last_.id, *gate);
    }
    last_.glide.makeImmediate();
    last_.glide.clear();
  }

  void rest(core::PerformanceEmitter& out) {
    end(out);
    last_ = {};
    sinceNote_ = {};
  }

  core::PerformanceNoteId note(core::PerformanceEmitter& out, u64 tick, u8 key, u32 delta, bool drum, bool tie) {
    const bool connected = !tie && continues_ && last_.id.valid() && !drum;
    const bool glide = !tie && !drum && settings_.portamentoTicks != 0 && previousKey_ && *previousKey_ != key;
    double startKey = previousKey_.value_or(key);
    if (connected) {
      startKey = out.currentPitchTransitionKey(last_.id).value_or(startKey);
    }
    const bool samePitch = connected && !glide && std::abs(startKey - key) < 0.000001;
    const u32 gate = std::max<u32>(1, delta > 2 ? delta - 2 : 0);
    const bool fullGate = settings_.mode != Mode::Normal;
    if (tie || connected) {
      out.setNoteEnd(last_.id, tick);
    }
    const auto id = out.note(core::NotePerformanceEvent{
        .key = static_cast<double>(key),
        .linearVelocity = 1.0,
        .durationTicks = fullGate ? std::max<u32>(1, delta) : gate,
        .extendsPrevious = tie || samePitch,
        .restartsEnvelope = !connected,
        // FF7 resets modulation phase even when key-on is suppressed.
        .restartsLfoPhase = true,
        .note = tie || samePitch ? last_.id : core::PerformanceNoteId{},
    });
    core::PitchSlideBinding pendingGlide;
    if (glide || (connected && !samePitch)) {
      auto slide = out.pitchSlide(id, startKey, key, glide ? settings_.portamentoTicks : 0);
      if (connected) {
        slide.continueFrom(last_.id).preferPitchBend();
        if (glide) {
          pendingGlide = slide;
        }
      }
    }
    last_ = Note{
        .id = id,
        .shortGateEnd = fullGate ? std::optional{tick + gate} : std::nullopt,
        .glide = pendingGlide,
    };
    continues_ = settings_.mode == Mode::Slur;
    sinceNote_ = {};
    if (!tie) {
      previousKey_ = key;
    }
    return id;
  }

private:
  struct Settings {
    Mode mode = Mode::Normal;
    u16 portamentoTicks = 0;
  };
  struct Note {
    core::PerformanceNoteId id;
    std::optional<u64> shortGateEnd;
    core::PitchSlideBinding glide;
  };

  Settings settings_;
  Settings sinceNote_;  // Commands after the last note survive its lookahead correction.
  Note last_;
  std::optional<u8> previousKey_;
  bool continues_ = false;
};

}  // namespace vgmtrans::formats::akao

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
  enum class Mode { Normal, Slur, Legato };
  struct Settings {
    Mode mode = Mode::Normal;
    u16 portamentoTicks = 0;
  };
  struct Note {
    core::PerformanceNoteId id, previous;
    double key = 0, startKey = 0;
    u64 tick = 0;
    std::optional<u64> shortGateEnd;
    core::PitchSlideBinding glide;
  };

public:
  // CC/D0/DA establish a new connection; CD/D1/DB end the preceding one.
  void connection(core::PerformanceEmitter& out, u8 opcode, u16 duration = 0) {
    if (opcode != 0xcc && opcode != 0xd0 && opcode != 0xda) {
      end(out);
      return;
    }
    settings_.mode = afterBoundary_.mode = opcode == 0xd0 ? Mode::Legato : Mode::Slur;
    continues_ = false;  // Every CC/D0/DA starts with a fresh attack.
    if (opcode == 0xda) {
      settings_.portamentoTicks = afterBoundary_.portamentoTicks = duration;
      previousKey_.reset();
    }
  }

  void end(core::PerformanceEmitter& out) {
    // Native lookahead clears modes before the preceding timed event. Apply
    // its gate/pitch correction here, following the VM's actual repeat path.
    // Preserve settings established since that event: they follow lookahead.
    settings_ = afterBoundary_;
    continues_ = false;
    if (auto gate = std::exchange(last_.shortGateEnd, std::nullopt)) {
      out.setNoteEnd(last_.id, *gate);
    }
    if (last_.glide.valid()) {
      last_.glide.interruptAt(last_.tick);
      out.at(last_.tick).pitchSlide(last_.id, last_.startKey, last_.key, 0)
          .continueFrom(last_.previous).preferPitchBend();
    }
  }

  void rest(core::PerformanceEmitter& out) {
    end(out);
    last_ = {};
    afterBoundary_ = {};
  }

  core::PerformanceNoteId note(core::PerformanceEmitter& out, u64 tick, u8 key, u32 delta, bool drum, bool tie) {
    const bool connected = !tie && continues_ && last_.id.valid() && !drum;
    const bool glide = !tie && !drum && settings_.portamentoTicks != 0 && previousKey_ && *previousKey_ != key;
    const double startKey = connected ? out.currentPitchTransitionKey(last_.id).value_or(previousKey_.value_or(key))
                                      : previousKey_.value_or(key);
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
        if (glide) pendingGlide = slide;
      }
    }
    last_ = Note{
        .id = id,
        .previous = last_.id,
        .key = static_cast<double>(key),
        .startKey = startKey,
        .tick = tick,
        .shortGateEnd = fullGate ? std::optional{tick + gate} : std::nullopt,
        .glide = pendingGlide,
    };
    continues_ = settings_.mode == Mode::Slur;
    afterBoundary_ = {};
    if (!tie) previousKey_ = key;
    return id;
  }

private:
  Settings settings_, afterBoundary_;
  Note last_;
  std::optional<u8> previousKey_;
  bool continues_ = false;
};

}  // namespace vgmtrans::formats::akao

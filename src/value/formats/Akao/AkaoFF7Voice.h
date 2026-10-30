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

// FF7 driver's key-on and lookahead rules.
// Bytecode decoding, instruments, and ADSR commands remain in AkaoSequence.cpp.
class AkaoFF7Voice {
public:
  // Slur suppresses key-on after the first note. Legato extends note durations but retains key-on.
  enum class Mode { Normal, Slur, Legato };

  // CC (Slur On) and D0 (Legato On) clear pending key-on suppression, so the next
  // note opcode triggers an attack even if the same mode was already active.
  void start(Mode mode) {
    settings_.mode = sinceNote_.mode = mode;
    continues_ = false;
  }

  // DA (Portamento On) enables slur and resets the remembered pitch. The first
  // note attacks without gliding; subsequent melodic notes glide without key-on.
  void portamento(u16 duration) {
    start(Mode::Slur);
    settings_.portamentoTicks = sinceNote_.portamentoTicks = duration;
    previousKey_.reset();
  }

  // Before starting a note, the FF7 driver scans subsequent sequence commands,
  // following repeat and jump targets. CB (Reset Effects), CD (Slur Off), D1 (Legato
  // Off), DB (Portamento Off), a rest, or A0 (End Track) restores that note's two-tick
  // gap and disables its portamento glide, leaving an immediate pitch change.
  // SequenceVm processes those commands after emitting the note, so this method
  // corrects that note's duration and pitch transition in the PerformanceSequence.
  void end(core::PerformanceEmitter& out) {
    // The driver's CD/D1 handlers are no-ops; their effect comes from lookahead.
    // CC/D0/DA after the preceding note execute after the driver's lookahead.
    // Preserve their settings: NOTE, CC, CD leaves slur enabled for the next note.
    settings_ = sinceNote_;
    continues_ = false;
    if (auto gate = std::exchange(last_.shortGateEnd, std::nullopt)) {
      out.setNoteEnd(last_.id, *gate);
    }
    last_.glide.makeImmediate();
    last_.glide.clear();
  }

  // Clear the previous note ID so a note after the rest cannot extend the note before it.
  void rest(core::PerformanceEmitter& out) {
    end(out);
    last_ = {};
    sinceNote_ = {};
  }

  // Create the performance note and pitch transition for an FF7 note or tie.
  // Retain the note and slide handles so end() can apply the driver's lookahead rules.
  core::PerformanceNoteId note(core::PerformanceEmitter& out, u64 tick, u8 key, u32 delta, bool drum, bool tie) {
    // Slur/portamento suppress key-on only for melodic notes. Ties reuse the previous note ID.
    const bool connected = !tie && continues_ && last_.id.valid() && !drum;
    const bool glide = !tie && !drum && settings_.portamentoTicks != 0 && previousKey_ && *previousKey_ != key;
    double startKey = previousKey_.value_or(key);
    if (connected) {
      // A previous pitch slide may not have reached its target at this sequence tick.
      startKey = out.currentPitchTransitionKey(last_.id).value_or(startKey);
    }
    const bool samePitch = connected && !glide && std::abs(startKey - key) < 0.000001;
    // Slur and legato use the full duration; normal mode leaves a two-tick gap.
    const u32 gate = std::max<u32>(1, delta > 2 ? delta - 2 : 0);
    const bool fullGate = settings_.mode != Mode::Normal;
    if (tie || connected) {
      out.setNoteEnd(last_.id, tick);
    }
    core::NotePerformanceEvent event{
        .key = static_cast<double>(key),
        .linearVelocity = 1.0,
        .durationTicks = fullGate ? std::max<u32>(1, delta) : gate,
        .extendsPrevious = tie || samePitch,
        .restartsEnvelope = !connected,
        // FF7 resets modulation phase even when key-on is suppressed.
        .restartsLfoPhase = true,
        .restartsVibratoLfoPhase = !tie,
    };
    const auto id = tie || connected ? out.continueVoice(last_.id, event) : out.note(event);
    core::PitchSlideBinding pendingGlide;
    if (glide || (connected && !samePitch)) {
      // Smooth slides currently interpolate semitones; FF7's driver interpolates
      // the fixed-point SPU pitch register. Immediate slurs are unaffected by this approximation.
      auto slide = out.pitchSlide(id, startKey, key, glide ? settings_.portamentoTicks : 0);
      if (connected) {
        // Prefer one held MIDI note plus pitch bend. MIDI portamento emits a new
        // Note On, whose ADSR retrigger behavior depends on the destination synth.
        slide.preferPitchBend();
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

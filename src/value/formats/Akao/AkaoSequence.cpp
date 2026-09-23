/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

constexpr u8 kNoteVelocity = 127;
constexpr u16 kDeltaTimeTable[] = {192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4};

// Akao repeats do not encode their destination. The decoder remembers only
// repeat starts so it can expose the same reachable blocks and source links.
struct RepeatStack {
  u8 layer = 0;
  std::array<Address, 4> begin{};
  std::array<u16, 4> completedPlays{};

  void start(Address address) {
    layer = static_cast<u8>((layer + 1) & 3);
    begin[layer] = address;
    completedPlays[layer] = 0;
  }

  [[nodiscard]] Address current() const { return begin[layer]; }
  [[nodiscard]] u16 currentCompletedPlays() const { return completedPlays[layer]; }

  void completeCurrentPlay() { ++completedPlays[layer]; }
  void finishFallthrough() { layer = static_cast<u8>((layer - 1) & 3); }
};

struct PendingPitchSlide {
  u16 durationTicks = 0;
  s8 semitones = 0;
};

// These are the registers whose values genuinely survive from one executed
// command to the next. Source bounds, version rules, and analysis do not.
struct TrackState {
  u8 octave = 4;
  s8 transpose = 0;
  s8 tuning = 0;
  bool slur = false;
  bool legato = false;
  u16 portamentoTicks = 0;
  bool drum = false;
  bool useOneTimeDuration = false;
  u8 oneTimeDuration = 0;
  u16 lastDeltaTime = 0;
  u16 fixedDuration = 0;
  u16 volume = 127;
  u16 expression = 127;
  u16 pan = 64;
  RepeatStack repeats;
  double tempoBpm = 120.0;
  std::optional<u8> previousKey;
  std::optional<u8> tieKey;
  std::optional<PendingPitchSlide> pendingPitchSlide;
};

[[nodiscard]] double stereoPositionFromPan(u8 rawPan) {
  const double rightGain = rawPan == 127 ? 1.0 : rawPan / 128.0;
  return (rightGain * 2.0) - 1.0;
}

[[nodiscard]] u16 akaoZeroAs256(u8 rawValue) {
  return rawValue == 0 ? 256 : rawValue;
}

[[nodiscard]] double akaoLinearControllerGain(u8 value) {
  return LevelScale::linearFromLinear(value / 127.0);
}

[[nodiscard]] double akaoTuningScale(s8 tuning) {
  const int divisor = tuning >= 0 ? 128 : 256;
  return tuning / static_cast<double>(divisor);
}

// Playback holds the few runtime services shared by several commands. One-off
// behavior stays beside the opcode that invokes it below.
struct Playback : SequencePlayback<TrackState> {
  void instrument(u32 bank, u32 program) { out.instrument(bank, program, true); }

  [[nodiscard]] u32 consumeDelta(u32 encodedDelta, u32 fallbackDelta) {
    u32 delta = encodedDelta;
    if (track.useOneTimeDuration) {
      delta = track.oneTimeDuration;
      track.useOneTimeDuration = false;
    }
    if (track.fixedDuration != 0) {
      delta = track.fixedDuration;
    }
    if (delta == 0) {
      delta = fallbackDelta;
    }
    track.lastDeltaTime = static_cast<u16>(delta);
    return delta;
  }

  [[nodiscard]] u32 soundingTicks(u32 delta, bool modern) const {
    if (modern || track.slur || track.legato) {
      return std::max<u32>(1, delta);
    }
    return std::max<u32>(1, delta > 2 ? delta - 2 : 0);
  }

  void queuePitchSlide(u16 durationTicks, s8 semitones) {
    track.pendingPitchSlide = PendingPitchSlide{
        .durationTicks = durationTicks,
        .semitones = semitones,
    };
  }

  void applyPendingPitchSlide(PerformanceNoteId note, double key) {
    const auto slide = std::exchange(track.pendingPitchSlide, std::nullopt);
    if (!slide || slide->durationTicks == 0 || slide->semitones == 0) {
      return;
    }
    out.pitchSlide(note, key, key + slide->semitones, slide->durationTicks).preferPitchBend();
  }

  template <class Emit>
  void controllerSlide(u16& previous, u16 target, u16 duration, Emit emit) {
    if (duration == 0) {
      previous = target;
      return;
    }
    const u64 startTick = vm.tick();
    const double increment =
        static_cast<double>(static_cast<int>(target) - static_cast<int>(previous)) / static_cast<double>(duration);
    for (u16 i = 0; i < duration; ++i) {
      const u8 value =
          static_cast<u8>(std::clamp(static_cast<int>(std::round(previous + increment * (i + 1))), 0, 127));
      emit(startTick + i, value);
    }
    previous = target;
  }
};

using AkaoCursor = CompilerCursor<Playback>;

// Akao stores many addresses as signed distances rather than absolute
// positions. This general helper converts one when the caller already knows
// what the address is for, such as a repeat, call, or conditional jump.
[[nodiscard]] Address relativeAddress(AkaoCursor& cursor, const AkaoProfile& profile, u32 operandOffset,
                                      std::string_view name, SemanticOperandRole role = SemanticOperandRole::Value) {
  const s16 relative = cursor.s16le(name);
  const Address destination{profile.relativeDestination(operandOffset, relative)};
  cursor.derived(fmt::format("{}_absolute", name), destination, SourceValueDisplay::Address, role);
  return destination;
}

// Read an unconditional jump whose destination is stored as a signed distance,
// record the resulting absolute address, and declare its playback behavior.
// Forward jumps skip ahead; backward jumps are marked as possible song loops.
[[nodiscard]] DecodedBytecodeCommand relativeJump(AkaoCursor& cursor, const AkaoProfile& profile,
                                                  u32 operandOffset, u32 commandAddress) {
  auto event = cursor.command("Jump", SequenceSemantic::Jump);
  const s16 relative = cursor.s16le("relative");
  const Address destination{profile.relativeDestination(operandOffset, relative)};
  const bool backward = destination.value <= commandAddress;
  const SemanticOperandRole role = backward ? SemanticOperandRole::LoopTarget : SemanticOperandRole::JumpTarget;
  cursor.derived("relative_absolute", destination, SourceValueDisplay::Address, role);
  return backward ? event.loopCandidate(destination) : event.jump(destination);
}

u32 relativePointer(AkaoCursor& cursor, const AkaoProfile& profile, u32 operandOffset, SemanticOperandRole role) {
  const s16 relative = cursor.s16le("relative");
  const Address destination{profile.relativeDestination(operandOffset, relative)};
  cursor.derived("relative_absolute", destination, SourceValueDisplay::Address, role);
  return destination.value;
}

[[nodiscard]] DecodedBytecodeCommand programArticulation(AkaoCursor& cursor, bool noAttack = false) {
  auto event = cursor.command(noAttack ? "Program Change w/o Attack" : "Program", SequenceSemantic::Program);
  const u8 articulation = cursor.u8("articulation", SemanticOperandRole::InstrumentProgram);
  // Bank 2 selects the sustain-only variant for F2 / subcommand 0A.
  const u32 bank = noAttack ? 2u : 0u;
  cursor.derived("bank", bank, SemanticOperandRole::InstrumentBank);
  return event.invoke<&Playback::instrument>({bank, articulation});
}

[[nodiscard]] DecodedBytecodeCommand customInstrumentTable(AkaoCursor& cursor, const AkaoProfile& profile,
                                                           u32 operandOffset) {
  auto event = cursor.command("Program Change (Key-Split Instrument)", SequenceSemantic::Program);
  cursor.derived("bank", 1u, SemanticOperandRole::InstrumentBank);
  const u32 table = relativePointer(cursor, profile, operandOffset, SemanticOperandRole::InstrumentTablePointer);
  return event.invoke([](Playback& playback, u32 offset) { playback.out.instrument(akaoMelodicTableIdentity(offset)); },
                      {table});
}

[[nodiscard]] DecodedBytecodeCommand drumKitOn(AkaoCursor& cursor, const AkaoProfile& profile, u32 operandOffset) {
  auto event = cursor.command("Drum Kit On", SequenceSemantic::Program);
  if (!profile.version3OrLater()) {
    relativePointer(cursor, profile, operandOffset, SemanticOperandRole::InstrumentTablePointer);
  }
  cursor.derived("bank", 127u, SemanticOperandRole::InstrumentBank);
  event.invoke<&Playback::instrument>({127u, 127u});
  return event.set<&TrackState::drum>(true);
}

[[nodiscard]] DecodedBytecodeCommand tempo(AkaoCursor& cursor, const AkaoProfile& profile) {
  auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
  const u16 raw = cursor.u16le("raw");
  const double bpm = cursor.derived("tempo", profile.tempoBpm(raw), SourceValueDisplay::BeatsPerMinute);
  const u32 micros = profile.tempoMicrosPerQuarter(raw);
  event.set<&TrackState::tempoBpm>(bpm);
  return event.emitTempo(micros);
}

[[nodiscard]] DecodedBytecodeCommand timeSignature(AkaoCursor& cursor) {
  auto event = cursor.command("Time Signature", SequenceSemantic::Meta);
  // The driver stores the metronome interval first and numerator second.
  const u8 ticksPerBeat = cursor.u8("ticks_per_beat");
  const u8 beatsPerMeasure = cursor.u8("beats_per_measure");
  if (ticksPerBeat != 0 && beatsPerMeasure != 0) {
    cursor.derived("numerator", beatsPerMeasure);
    cursor.derived("denominator", static_cast<u8>((kAkaoPpqn * 4) / ticksPerBeat));
    cursor.derived("midi_clocks_per_metronome_click", ticksPerBeat);
  }
  return event.invoke(
      [](Playback& playback, u8 ticks, u8 beats) {
        if (ticks == 0 || beats == 0) {
          return;
        }
        const u8 denominator = static_cast<u8>((kAkaoPpqn * 4) / ticks);
        playback.out.timeSignature(beats, denominator, ticks);
      },
      {ticksPerBeat, beatsPerMeasure});
}

[[nodiscard]] DecodedBytecodeCommand repeatBranch(AkaoCursor& cursor, const AkaoProfile& profile,
                                                  u32 operandOffset) {
  auto event = cursor.command("Loop Branch", SequenceSemantic::RepeatBreak);
  const u16 count = cursor.resolved("count", cursor.rawU8("raw_count"), akaoZeroAs256);
  const Address destination =
      relativeAddress(cursor, profile, operandOffset, "relative", SemanticOperandRole::RepeatTarget);
  event.discoverTarget(destination);
  return event.invoke(
      [](Playback& playback, u16 matchingPlay, Address branchDestination) -> Effects {
        if (playback.track.repeats.currentCompletedPlays() + 1 == matchingPlay) {
          return playback.vm.finiteBranch(branchDestination);
        }
        return {};
      },
      {count, destination});
}

[[nodiscard]] DecodedBytecodeCommand passiveBranch(AkaoCursor& cursor, const AkaoProfile& profile,
                                                   u32 operandOffset, std::string_view label,
                                                   std::string_view conditionName) {
  auto event = cursor.command(label, SequenceSemantic::Jump);
  cursor.u8(conditionName);
  const Address destination =
      relativeAddress(cursor, profile, operandOffset + 1, "relative", SemanticOperandRole::JumpTarget);
  return event.discoverTarget(destination);
}

[[nodiscard]] DecodedBytecodeCommand decodeSubEvent(AkaoCursor& cursor, u32 begin,
                                                    const AkaoProfile& profile) {
  const u8 sub = cursor.u8("sub_event", SourceValueDisplay::Hex);
  if (!cursor.ok()) {
    return cursor.unsupported("Truncated Sub Event").stop();
  }

  switch (sub) {
    case 0x00:
      return tempo(cursor, profile);
    case 0x01: {
      auto event = cursor.command("Tempo Fade", SequenceSemantic::Tempo);
      const u16 duration = cursor.resolved("duration_ticks", cursor.rawU8("duration"), akaoZeroAs256);
      const u16 raw = cursor.u16le("raw");
      const double bpm = cursor.derived("target_tempo", profile.tempoBpm(raw), SourceValueDisplay::BeatsPerMinute);
      const u32 micros = profile.tempoMicrosPerQuarter(raw);
      return event.invoke(
          [](Playback& playback, u16 fadeTicks, double targetBpm, u32 targetMicros) {
            const auto automation =
                playback.out.fade(PerformanceAutomationTarget::Tempo, static_cast<double>(targetMicros), fadeTicks);
            const u64 startTick = playback.vm.tick();
            const double increment = (targetBpm - playback.track.tempoBpm) / fadeTicks;
            for (u16 i = 0; i < fadeTicks; ++i) {
              const double fadedBpm = playback.track.tempoBpm + increment * (i + 1);
              const u32 fadedMicros = static_cast<u32>(std::round(60000000.0 / std::max(1.0, fadedBpm)));
              automation.at(playback.out, startTick + i).tempo(fadedMicros);
            }
            playback.track.tempoBpm = targetBpm;
          },
          {duration, bpm, micros});
    }
    case 0x04:
      return drumKitOn(cursor, profile, begin + 2);
    case 0x05:
      return cursor.command("Drum Kit Off", SequenceSemantic::Program).set<&TrackState::drum>(false);
    case 0x06:
      return relativeJump(cursor, profile, begin + 2, begin);
    case 0x07:
      return passiveBranch(cursor, profile, begin + 2, "CPU Conditional Jump", "condition");
    case 0x08:
      return repeatBranch(cursor, profile, begin + 3);
    case 0x09:
      return passiveBranch(cursor, profile, begin + 2, "Loop Break", "count");
    case 0x0a:
      return programArticulation(cursor, true);
    case 0x0e: {
      if (profile.version32()) {
        return cursor.command("Play Pattern", SequenceSemantic::Call)
            .call(relativeAddress(cursor, profile, begin + 2, "relative", SemanticOperandRole::CallTarget));
      }
      return cursor.ignored("Unknown FE 0E", profile.subOperandBytes(sub), "unknown-fe-0e");
    }
    case 0x0f: {
      if (profile.version32()) {
        return cursor.command("End Pattern", SequenceSemantic::Return).return_();
      }
      return cursor.ignored("Unknown FE 0F", profile.subOperandBytes(sub), "unknown-fe-0f");
    }
    case 0x12: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u16 duration = cursor.resolved("duration_ticks", cursor.rawU8("duration"), akaoZeroAs256);
      const u8 target = cursor.u8("target_volume");
      return event.invoke(
          [](Playback& playback, u16 fadeTicks, u8 targetVolume) {
            const auto automation = playback.out.fade(PerformanceAutomationTarget::Level,
                                                      akaoLinearControllerGain(targetVolume), fadeTicks);
            playback.controllerSlide(playback.track.volume, targetVolume, fadeTicks, [&](u64 tick, u8 value) {
              automation.at(playback.out, tick).level(akaoLinearControllerGain(value));
            });
          },
          {duration, target});
    }
    case 0x14: {
      if (profile.version3OrLater()) {
        auto event = cursor.command("Program Change (Key-Split Instrument)", SequenceSemantic::Program);
        const u8 program = cursor.u8("program", SemanticOperandRole::InstrumentProgram);
        cursor.derived("bank", 1u, SemanticOperandRole::InstrumentBank);
        return event.invoke<&Playback::instrument>({1u, program});
      }
      return customInstrumentTable(cursor, profile, begin + 2);
    }
    case 0x15:
      return timeSignature(cursor);
    default:
      return cursor.ignored(fmt::format("Sub Event {:02X}", sub), profile.subOperandBytes(sub), "sub-event");
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, const AkaoProfile& profile,
                                                   RepeatStack& repeats, std::vector<Diagnostic>* diagnostics) {
  AkaoCursor cursor(reader, begin, end, commandKindPrefix(profile.version), diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 status = cursor.opcode();
  if (profile.isNoteOpcode(status)) {
    const bool inlineDuration = profile.noteHasInlineDuration(status);
    const u8 noteByte = inlineDuration ? static_cast<u8>((status - 0xf0) * 11) : status;
    const bool rest = noteByte >= 0x8f;
    // Twelve pitches each have eleven durations: 0x83 is the final B note.
    // SaGa Frontier SCUS_942.30 compares against 0x84 at 0x8004934c.
    const bool tie = !rest && noteByte >= 0x84;
    const u32 fallbackDelta = kDeltaTimeTable[noteByte % 11];
    const bool modern = profile.version3OrLater();

    if (rest) {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      const u32 delta = inlineDuration ? cursor.u8("duration") : fallbackDelta;
      return event.invoke(
          [](Playback& playback, u32 encodedDelta, u32 defaultDelta) -> Effects {
            const u32 duration = playback.consumeDelta(encodedDelta, defaultDelta);
            playback.track.tieKey.reset();
            return Effects::wait(duration);
          },
          {delta, fallbackDelta});
    }
    if (tie) {
      auto event = cursor.command("Tie", SequenceSemantic::Note);
      const u32 delta = inlineDuration ? cursor.u8("duration") : fallbackDelta;
      return event.invoke(
          [](Playback& playback, u32 encodedDelta, u32 defaultDelta, bool modernDriver) -> Effects {
            const u32 duration = playback.consumeDelta(encodedDelta, defaultDelta);
            if (playback.track.tieKey) {
              const PerformanceNoteId note =
                  playback.out.note(*playback.track.tieKey, LevelScale::linearFromMidi7(kNoteVelocity),
                                    playback.soundingTicks(duration, modernDriver), true);
              playback.applyPendingPitchSlide(note, *playback.track.tieKey);
            }
            return Effects::wait(duration);
          },
          {delta, fallbackDelta, modern});
    }

    auto event = cursor.command("Note", SequenceSemantic::Note);
    // The opcode stores a scale step. Octave and transposition are applied
    // later because their values depend on the path taken through the track.
    const u8 relativeKey = cursor.opcodeValue("scale_step", static_cast<u8>(noteByte / 11));
    const u32 delta = inlineDuration ? cursor.u8("duration") : fallbackDelta;
    return event.invoke(
        [](Playback& playback, u8 scaleStep, u32 encodedDelta, u32 defaultDelta, bool modernDriver) -> Effects {
          const u32 duration = playback.consumeDelta(encodedDelta, defaultDelta);
          const u8 sourceKey = playback.track.drum && !modernDriver
                                   ? static_cast<u8>(24 + scaleStep)
                                   : static_cast<u8>(playback.track.octave * 12 + scaleStep);
          const u8 key =
              static_cast<u8>(std::clamp<int>(static_cast<int>(sourceKey) + playback.track.transpose, 0, 127));
          const PerformanceNoteId note = playback.out.note(key, LevelScale::linearFromMidi7(kNoteVelocity),
                                                           playback.soundingTicks(duration, modernDriver));
          if (playback.track.portamentoTicks != 0 && playback.track.previousKey && *playback.track.previousKey != key) {
            playback.out.pitchSlide(note, *playback.track.previousKey, key, playback.track.portamentoTicks);
          }
          playback.applyPendingPitchSlide(note, key);
          playback.track.previousKey = key;
          playback.track.tieKey = key;
          return Effects::wait(duration);
        },
        {relativeKey, delta, fallbackDelta, modern});
  }

  if (status >= 0x9a && status <= 0x9f) {
    return cursor.unsupported("Undefined Akao event").stop();
  }

  if (profile.isSubEventPrefix(status)) {
    return decodeSubEvent(cursor, begin, profile);
  }

  switch (status) {
    case 0xa0:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0xa1:
      return programArticulation(cursor);
    case 0xa2: {
      auto event = cursor.command("Next Note Length", SequenceSemantic::State);
      event.set<&TrackState::oneTimeDuration>(cursor.u8("duration"));
      return event.set<&TrackState::useOneTimeDuration>(true);
    }
    case 0xa3: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const u8 volume = cursor.u8("volume");
      event.set<&TrackState::volume>(volume);
      return event.emitLevel(akaoLinearControllerGain(volume));
    }
    case 0xa4: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      const u16 duration = cursor.resolved("duration_ticks", cursor.rawU8("duration"), akaoZeroAs256);
      const s8 semitones = cursor.s8("semitones");
      return event.invoke<&Playback::queuePitchSlide>({duration, semitones});
    }
    case 0xa5:
      return cursor.command("Octave", SequenceSemantic::State).set<&TrackState::octave>(cursor.u8("octave"));
    case 0xa6:
      return cursor.command("Octave Up", SequenceSemantic::State).add<&TrackState::octave>(1u);
    case 0xa7: {
      return cursor.command("Octave Down", SequenceSemantic::State).invoke([](Playback& playback) {
        if (playback.track.octave > 0) {
          --playback.track.octave;
        }
      });
    }
    case 0xa8: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      const u8 expression = cursor.u8("expression");
      event.set<&TrackState::expression>(expression);
      return event.emitExpression(akaoLinearControllerGain(expression));
    }
    case 0xa9: {
      auto event = cursor.command("Expression Fade", SequenceSemantic::Level);
      const u16 duration = cursor.resolved("duration_ticks", cursor.rawU8("duration"), akaoZeroAs256);
      const u8 target = cursor.u8("target_expression");
      return event.invoke(
          [](Playback& playback, u16 fadeTicks, u8 targetExpression) {
            const auto automation = playback.out.fade(PerformanceAutomationTarget::Expression,
                                                      akaoLinearControllerGain(targetExpression), fadeTicks);
            playback.controllerSlide(playback.track.expression, targetExpression, fadeTicks, [&](u64 tick, u8 value) {
              automation.at(playback.out, tick).expression(akaoLinearControllerGain(value));
            });
          },
          {duration, target});
    }
    case 0xaa: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 pan = cursor.u8("pan");
      event.set<&TrackState::pan>(pan);
      return event.emitPan(stereoPositionFromPan(pan));
    }
    case 0xab: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const u16 duration = cursor.resolved("duration_ticks", cursor.rawU8("duration"), akaoZeroAs256);
      const u8 target = cursor.u8("target_pan");
      return event.invoke(
          [](Playback& playback, u16 fadeTicks, u8 targetPan) {
            const double targetPosition = stereoPositionFromPan(targetPan);
            const auto automation = playback.out.fade(PerformanceAutomationTarget::Pan, targetPosition, fadeTicks);
            playback.controllerSlide(playback.track.pan, targetPan, fadeTicks, [&](u64 tick, u8 value) {
              automation.at(playback.out, tick).pan(stereoPositionFromPan(value));
            });
          },
          {duration, target});
    }
    case 0xc0:
      return cursor.command("Transpose", SequenceSemantic::Pitch).set<&TrackState::transpose>(cursor.s8("semitones"));
    case 0xc1: {
      auto event = cursor.command("Transpose (Relative)", SequenceSemantic::Pitch);
      const s8 semitones = cursor.s8("semitones");
      return event.invoke(
          [](Playback& playback, s8 relative) {
            playback.track.transpose =
                static_cast<s8>(std::clamp<int>(static_cast<int>(playback.track.transpose) + relative, -128, 127));
          },
          {semitones});
    }
    case 0xc2:
      return cursor.sourceOnly("Reverb On");
    case 0xc3:
      return cursor.sourceOnly("Reverb Off");
    case 0xc8: {
      auto event = cursor.command("Repeat Start", SequenceSemantic::Repeat);
      const Address start = cursor.nextAddress();
      // Decoding and playback walk the track independently, so each needs its
      // own copy of the repeat stack.
      repeats.start(start);
      return event.invoke([](Playback& playback, Address address) { playback.track.repeats.start(address); }, {start});
    }
    case 0xc9: {
      auto event = cursor.command("Repeat Until", SequenceSemantic::Repeat);
      const u16 count = cursor.resolved("count", cursor.rawU8("raw_count"), akaoZeroAs256);
      const Address target = repeats.current();
      repeats.completeCurrentPlay();
      repeats.finishFallthrough();
      cursor.derived("destination", target, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      event.discoverTarget(target);
      return event.invoke(
          [](Playback& playback, u16 totalPlays) -> Effects {
            const u8 slot = playback.track.repeats.layer;
            const Address start = playback.track.repeats.current();
            playback.track.repeats.completeCurrentPlay();
            Effects effects = playback.vm.countedRepeatUntil(slot, totalPlays, start);
            if (!effects.flowOverride) {
              playback.track.repeats.finishFallthrough();
            }
            return effects;
          },
          {count});
    }
    case 0xca: {
      auto event = cursor.command("Repeat Again", SequenceSemantic::Repeat);
      const Address target = repeats.current();
      cursor.derived("destination", target, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.loopCandidate(target);
    }
    case 0xcc:
      return cursor.command("Slur On", SequenceSemantic::State).set<&TrackState::slur>(true);
    case 0xcd:
      return cursor.command("Slur Off", SequenceSemantic::State).set<&TrackState::slur>(false);
    case 0xd0:
      return cursor.command("Legato On", SequenceSemantic::State).set<&TrackState::legato>(true);
    case 0xd1:
      return cursor.command("Legato Off", SequenceSemantic::State).set<&TrackState::legato>(false);
    case 0xd8: {
      auto event = cursor.command("Tuning", SequenceSemantic::Pitch);
      const s8 tuning = cursor.s8("tuning");
      event.set<&TrackState::tuning>(tuning);
      // Preserve Akao's original pitch resolution by first rounding through
      // the 14-bit bend value used with its twelve-semitone bend range.
      constexpr double pitchBendRangeSemitones = 12.0;
      const double percent = akaoTuningScale(tuning) / std::log(2.0);
      const auto bend = static_cast<s16>(std::clamp(static_cast<int>(percent * 8192.0), -8192, 8191));
      return event.emitPitchBend((bend / 8192.0) * pitchBendRangeSemitones);
    }
    case 0xd9: {
      auto event = cursor.command("Tuning (Relative)", SequenceSemantic::Pitch);
      const s8 tuning = cursor.s8("tuning");
      return event.invoke(
          [](Playback& playback, s8 relative) {
            playback.track.tuning =
                static_cast<s8>(std::clamp<int>(static_cast<int>(playback.track.tuning) + relative, -128, 127));
            const double cents = (akaoTuningScale(playback.track.tuning) / std::log(2.0)) * 1200.0;
            playback.out.tuning(cents);
          },
          {tuning});
    }
    case 0xda: {
      auto event = cursor.command("Portamento On", SequenceSemantic::Portamento);
      const u16 speed = cursor.resolved("ticks", cursor.rawU8("speed"), akaoZeroAs256);
      return event.set<&TrackState::portamentoTicks>(speed);
    }
    case 0xdb:
      return cursor.command("Portamento Off", SequenceSemantic::Portamento).set<&TrackState::portamentoTicks>(0);
    case 0xdc: {
      auto event = cursor.command("Fixed Note Length", SequenceSemantic::State);
      const s8 relativeLength = cursor.s8("relative_length");
      return event.invoke(
          [](Playback& playback, s8 relative) {
            playback.track.fixedDuration =
                static_cast<u16>(std::clamp<int>(static_cast<int>(playback.track.lastDeltaTime) + relative, 1, 255));
          },
          {relativeLength});
    }
    case 0xe8:
      if (profile.legacyFamily()) {
        return tempo(cursor, profile);
      }
      break;
    case 0xea:
      if (profile.legacyFamily()) {
        auto event = cursor.sourceOnly("Reverb Depth");
        cursor.u16le("depth");
        return event;
      }
      break;
    case 0xec:
      if (profile.legacyFamily()) {
        return drumKitOn(cursor, profile, begin + 1);
      }
      break;
    case 0xed:
      if (profile.legacyFamily()) {
        return cursor.command("Drum Kit Off", SequenceSemantic::Program).set<&TrackState::drum>(false);
      }
      break;
    case 0xee:
      if (profile.legacyFamily()) {
        return relativeJump(cursor, profile, begin + 1, begin);
      }
      break;
    case 0xef:
      if (profile.legacyFamily()) {
        return passiveBranch(cursor, profile, begin + 1, "CPU Conditional Jump", "condition");
      }
      break;
    case 0xf0:
      if (profile.legacyFamily()) {
        return repeatBranch(cursor, profile, begin + 2);
      }
      break;
    case 0xf1:
      if (profile.legacyFamily()) {
        return passiveBranch(cursor, profile, begin + 1, "Loop Break", "count");
      }
      break;
    case 0xf2:
      if (profile.legacyFamily()) {
        return programArticulation(cursor, true);
      }
      break;
    case 0xf4:
      if (profile.version == AkaoPs1Version::Version1_0) {
        auto event = cursor.command("Overlay Voice On", SequenceSemantic::Program);
        const u8 primaryArt = cursor.u8("primary_articulation", SemanticOperandRole::InstrumentProgram);
        cursor.u8("secondary_articulation", SemanticOperandRole::InstrumentProgram);
        cursor.derived("bank", 0u, SemanticOperandRole::InstrumentBank);
        return event.invoke<&Playback::instrument>({0u, primaryArt});
      }
      break;
    case 0xf5:
      if (profile.version == AkaoPs1Version::Version1_0) {
        return cursor.sourceOnly("Overlay Voice Off");
      }
      break;
    case 0xf6:
      if (profile.version == AkaoPs1Version::Version1_0) {
        auto event = cursor.sourceOnly("Overlay Volume Balance");
        cursor.u8("balance");
        return event;
      }
      break;
    case 0xf7:
      if (profile.version == AkaoPs1Version::Version1_0) {
        auto event = cursor.sourceOnly("Overlay Volume Balance Fade");
        const u8 rawDuration = cursor.u8("duration");
        cursor.derived("duration_ticks", akaoZeroAs256(rawDuration));
        cursor.u8("balance");
        return event;
      }
      break;
    case 0xfc:
      if (profile.version == AkaoPs1Version::Version1_1) {
        return customInstrumentTable(cursor, profile, begin + 1);
      }
      break;
    case 0xfd:
      if (profile.legacyFamily()) {
        return timeSignature(cursor);
      }
      break;
    default:
      break;
  }

  return cursor.ignored("Akao Event", profile.directOperandBytes(status), "event");
}

}  // namespace

std::optional<AkaoSequenceLayout> readAkaoSequenceLayout(const ScanInput& input, u32 offset) {
  const ByteReader reader = input.reader;
  if (!reader.has(offset, 0x10) || reader.be32(offset) != kAkaoSignature || reader.le16(offset + 6) == 0) {
    return std::nullopt;
  }

  const auto plausibleHeader = [&](const AkaoProfile& profile) {
    const u32 bitsOffset = profile.trackAllocationBitsOffset();
    if (!reader.has(offset + bitsOffset, 4)) {
      return false;
    }
    if (!profile.version3OrLater()) {
      return (reader.le32(offset + bitsOffset) & ~0xffffffu) == 0;
    }
    return reader.has(offset, 0x40) && reader.le32(offset + 0x28) == 0 && reader.le32(offset + 0x2c) == 0 &&
           reader.le32(offset + 0x38) == 0 && reader.le32(offset + 0x3c) == 0;
  };

  // First validate the profile suggested by the bytes themselves. A known
  // source must not make an unrelated AKAO signature look sequence-like.
  const AkaoProfile candidateProfile{.version = guessSequenceVersion(reader, offset)};
  if (!plausibleHeader(candidateProfile)) {
    return std::nullopt;
  }

  // The source executable identifies the driver exactly when available. Raw
  // sequence files fall back to the few header differences the versions expose.
  AkaoPs1Version version = determineVersionFromSource(input.source);
  if (version == AkaoPs1Version::Unknown) {
    version = guessSequenceVersion(reader, offset);
  }
  if (version == AkaoPs1Version::Unknown) {
    return std::nullopt;
  }

  const AkaoProfile profile{.version = version};
  if (!plausibleHeader(profile)) {
    return std::nullopt;
  }
  const u32 trackBits = reader.le32(offset + profile.trackAllocationBitsOffset());
  const u32 trackCount = std::popcount(trackBits);
  if (trackCount == 0) {
    return std::nullopt;
  }
  const u32 declaredLength = profile.sequenceLength(reader, offset);
  if (declaredLength == 0) {
    return std::nullopt;
  }

  const u64 pointerTableEnd = static_cast<u64>(profile.trackHeaderOffset()) + trackCount * 2ull;
  const u64 pointerTable = static_cast<u64>(offset) + profile.trackHeaderOffset();
  if (pointerTableEnd > declaredLength || !reader.has(pointerTable, trackCount * 2ull)) {
    return std::nullopt;
  }

  AkaoSequenceLayout layout{
      .header =
          AkaoSequenceHeader{
              .offset = offset,
              .length = static_cast<u32>(std::min<u64>(declaredLength, reader.size() - offset)),
              .version = version,
              .sequenceId = reader.le16(offset + 4),
              .trackBits = trackBits,
              .trackHeaderOffset = profile.trackHeaderOffset(),
          },
  };
  if (profile.version3OrLater()) {
    layout.header.sampleSetId = reader.le16(offset + 0x14);
    const u32 instrumentTable = reader.le32(offset + 0x30);
    const u32 drumTable = reader.le32(offset + 0x34);
    if (instrumentTable != 0) {
      layout.header.soundBankOffset = offset + 0x30 + instrumentTable;
    }
    if (drumTable != 0) {
      layout.header.drumSetOffset = offset + 0x34 + drumTable;
    }
  }

  layout.trackAddresses.reserve(trackCount);
  for (u32 i = 0; i < trackCount; ++i) {
    const u32 pointerOffset = layout.header.trackHeaderOffset + i * 2;
    const u32 base = pointerOffset + (profile.version3OrLater() ? 0 : 2);
    const u32 relative = reader.le16(offset + pointerOffset);
    const u64 trackRelative = static_cast<u64>(base) + relative;
    const u64 trackAddress = static_cast<u64>(offset) + trackRelative;
    if (trackRelative < pointerTableEnd || trackRelative >= declaredLength || !reader.has(trackAddress, 1)) {
      return std::nullopt;
    }
    layout.trackAddresses.push_back(static_cast<u32>(trackAddress));
  }
  return layout;
}

SequenceProgramConfig makeAkaoConfig(AkaoPs1Version version) {
  const std::string prefix = commandKindPrefix(version);
  const PanLaw panLaw = defaultPanLaw(version);
  return SequenceProgramConfig{
      .commandKindPrefix = prefix,
      .timebase = Timebase{.ppqn = kAkaoPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kAkaoMaxTrackCommands,
              .panLaw = panLaw,
              .initialLevel = 1.0,
              .initialStereoBalance =
                  panLaw == PanLaw::ConstantSum ? std::optional{StereoBalance{0.5, 0.5}} : std::nullopt,
              .initialPitchBendRangeSemitones = 12,
          },
  };
}

SequenceRuntime akaoSequenceRuntime() {
  return makeCompiledRuntime<Playback>();
}

namespace {

void collectReferences(const DecodedBytecodeCommand& command, AkaoSequenceReferences& references) {
  const auto unsignedValue = [](const SemanticOperand& operand) -> std::optional<u32> {
    const auto* value = std::get_if<u64>(&operand.value);
    return value && *value <= std::numeric_limits<u32>::max() ? std::optional{static_cast<u32>(*value)} : std::nullopt;
  };

  std::optional<u32> bank;
  std::optional<u32> instrumentTable;
  std::vector<u32> programs;
  for (const auto& operand : command.operands) {
    const auto value = unsignedValue(operand);
    if (!value) {
      continue;
    }
    switch (operand.role) {
      case SemanticOperandRole::InstrumentBank:
        bank = *value;
        break;
      case SemanticOperandRole::InstrumentTablePointer:
        instrumentTable = *value;
        break;
      case SemanticOperandRole::InstrumentProgram:
        programs.push_back(*value);
        break;
      default:
        break;
    }
  }

  if (instrumentTable) {
    (bank == 127 ? references.drumInstrumentTableOffsets : references.customInstrumentTableOffsets)
        .insert(*instrumentTable);
  }
  if ((bank != 0 && bank != 2) || programs.empty()) {
    return;
  }
  references.usesIndividualArticulations = true;
  if (bank == 2) {
    references.noAttackArticulationIds.insert(programs.begin(), programs.end());
  }
  for (const u32 articulation : programs) {
    if (articulation != 0) {
      references.individualArticulationIds.insert(articulation);
    }
  }
}

}  // namespace

TrackProgram decodeAkaoTrack(AkaoPs1Version version, const TrackDecodeScope& tracks, u32 trackIndex, u32 startOffset,
                             std::vector<Diagnostic>* diagnostics, AkaoSequenceReferences* references) {
  RepeatStack repeats;
  const AkaoProfile profile{.version = version};
  const u32 bytecodeEnd = tracks.bytecodeEnd == std::numeric_limits<u32>::max() ? static_cast<u32>(tracks.reader.size())
                                                                                : tracks.bytecodeEnd;
  const auto command = [&](u32 offset) {
    auto decoded = decodeCommand(tracks.reader, offset, bytecodeEnd, profile, repeats, diagnostics);
    if (references != nullptr) {
      collectReferences(decoded, *references);
    }
    return decoded;
  };
  return tracks.decode(trackIndex, startOffset, command);
}

// The layout is deliberately read before a draft is created. From this point
// onward malformed track data is part of a recognized sequence and is reported
// through the shared diagnostics rather than retracting the asset.
AkaoSequenceParse parseAkaoSequence(const ScanInput& input, AssetId id, const AkaoSequenceLayout& layout,
                                    SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const ByteReader reader = input.reader;
  const u32 offset = layout.header.offset;
  AkaoSequenceAnalysis analysis{.header = layout.header};
  const AkaoProfile profile{.version = analysis.header.version};
  const u32 sequenceEnd = offset + analysis.header.length;
  auto config = makeAkaoConfig(analysis.header.version);
  config.behavior.panLaw = determinePanLawFromSource(input.source, analysis.header.version);
  config.behavior.initialStereoBalance =
      config.behavior.panLaw == PanLaw::ConstantSum ? std::optional{StereoBalance{0.5, 0.5}} : std::nullopt;

  SequenceDecodeSession sequence(reader, config, id, reader.range(offset, analysis.header.trackHeaderOffset), sourceMap,
                                 kAkaoMaxTrackCommands, sequenceEnd);
  auto header = sequence.header()
                    .label("AKAO Sequence Header")
                    .kind("akao-sequence-header")
                    .field("sequence_id", reader.range(offset + 4, 2), analysis.header.sequenceId)
                    .field("size", reader.range(offset + 6, 2), analysis.header.length)
                    .field("track_bits", reader.range(offset + profile.trackAllocationBitsOffset(), 4),
                           analysis.header.trackBits, SourceValueDisplay::Hex);
  if (analysis.header.sampleSetId) {
    header.field("sample_set_id", reader.range(offset + 0x14, 2), *analysis.header.sampleSetId);
  }

  for (u32 trackIndex = 0; trackIndex < layout.trackAddresses.size(); ++trackIndex) {
    sequence.addTrack(decodeAkaoTrack(analysis.header.version, sequence.trackScope(), trackIndex,
                                      layout.trackAddresses[trackIndex], diagnostics, &analysis.references));
  }
  return AkaoSequenceParse{
      .program = sequence.finish(akaoSequenceRuntime()),
      .analysis = std::move(analysis),
  };
}

}  // namespace vgmtrans::formats::akao

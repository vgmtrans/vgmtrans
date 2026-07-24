/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiArcade/KonamiArcade.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceMotion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace vgmtrans::formats::konami_arcade {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 32768;

[[nodiscard]] double attenuationGain(int attenuation) {
  const int clamped = std::clamp(attenuation, 0, 255);
  return std::pow(10.0, (-36.0 * clamped / 64.0) / 20.0);
}

[[nodiscard]] double volumeGain(u8 raw) {
  return attenuationGain(static_cast<u8>(~raw) & 0x7f);
}

[[nodiscard]] u8 panIndex(u8 raw) {
  if (raw >= 0x81 && raw <= 0x8f) {
    return raw - 0x81;
  }
  if (raw >= 0x11 && raw <= 0x1f) {
    return raw - 0x11;
  }
  return 7;
}

[[nodiscard]] std::pair<double, double> stereoGains(u8 raw) {
  const double divisor = std::sqrt(14.0);
  const u8 index = panIndex(raw);
  return {
      std::sqrt(static_cast<double>(14 - index)) / divisor,
      std::sqrt(static_cast<double>(index)) / divisor,
  };
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(double nmiRateHertz, double tempo) {
  if (tempo <= 0.0 || nmiRateHertz <= 0.0 || !std::isfinite(tempo) || !std::isfinite(nmiRateHertz)) {
    return 60'000'000;
  }
  const double value = (256.0 / tempo) / nmiRateHertz * kKonamiArcadePpqn * 1'000'000.0;
  return static_cast<u32>(std::clamp<double>(std::lround(value), 1.0, 60'000'000.0));
}

[[nodiscard]] double tickMilliseconds(double nmiRateHertz, double tempo) {
  if (tempo <= 0.0 || nmiRateHertz <= 0.0) {
    return 0.0;
  }
  return (256.0 / tempo) / nmiRateHertz * 1000.0;
}

[[nodiscard]] double portamentoMilliseconds(u8 ticks, double nmiRateHertz, double tempo) {
  // The driver stores this conversion in an integer before writing the two
  // portamento-time controller bytes.
  return std::trunc(ticks * tickMilliseconds(nmiRateHertz, tempo));
}

struct TrackState {
  bool percussionFlag1 = false;
  bool percussionFlag2 = false;
  u8 previousDelta = 0;
  u8 durationRate = 0;
  u8 releaseRate = 0;
  u8 program = 0;
  s32 transpose = 0;
  std::array<Address, 2> loopStart;
  std::array<s16, 2> loopAttenuation{};
  std::array<s16, 2> loopTranspose{};
  u8 pan = 8;
  double tempo = 120.0;
  double nmiRateHertz = 0.0;
  PerformanceBoundMotion<SequenceAutomatedValue<double>> volumeMotion;
  PerformanceBoundMotion<SequenceAutomatedValue<double>> panMotion;
  PerformanceBoundMotion<SequenceAutomatedValue<double>> tempoMotion;
  SequenceAutomatedValue<double> pitchMotion;
  std::optional<double> previousKey;
  PerformanceNoteId previousNote;
  u64 previousNoteStart = 0;
  u32 previousNoteDuration = 0;
  double previousNoteGain = 1.0;
  bool previousTied = false;
  bool durationTieCanceled = false;
  u8 portamentoTime = 0;
  u8 slideDelay = 0;
  u8 slideDuration = 0;
  s8 slideDepth = 0;
  Address subroutineStart;
  Address subroutineReturn;
  bool definingSubroutine = false;
  bool inSubroutine = false;
  u8 callDepth = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  [[nodiscard]] bool percussion() const { return track.percussionFlag1 || track.percussionFlag2; }

  void setPercussion(u8 flag, bool enabled) {
    if (flag == 0) {
      track.percussionFlag1 = enabled;
    } else {
      track.percussionFlag2 = enabled;
    }
    const bool isPercussion = percussion();
    if (!enabled && isPercussion) {
      return;
    }
    if (isPercussion) {
      out.instrument(InstrumentIdentity{.domain = std::string(kKonamiArcadeInstrumentDomain), .key = 0x100});
    } else {
      out.instrument(InstrumentIdentity{.domain = std::string(kKonamiArcadeInstrumentDomain), .key = track.program});
    }
    track.durationTieCanceled = true;
  }

  void programChange(u8 value) {
    track.program = value;
    if (!percussion()) {
      out.instrument(InstrumentIdentity{.domain = std::string(kKonamiArcadeInstrumentDomain), .key = value});
    }
    track.durationTieCanceled = true;
  }

  void note(u8 sourceKey, u8 delta, u8 velocity, u8 drumDuration, u8 drumPan) {
    const bool isDrum = percussion();
    const int loopAttenuation = track.loopAttenuation[0] + track.loopAttenuation[1];
    const int attenuation = 127 - velocity + loopAttenuation;
    const double gain = attenuationGain(attenuation);

    u32 duration = delta;
    if (track.durationRate == 0 && isDrum && sourceKey < 46) {
      duration = static_cast<u32>(delta) * drumDuration / 100;
      if (track.pan == 0) {
        const auto [left, right] = stereoGains(drumPan | 0x10);
        out.stereoBalance(left, right);
      }
    } else if (track.durationRate != 0 && track.releaseRate != 0) {
      duration = std::max<u32>(1, static_cast<u32>(delta) * track.durationRate / 100);
    }

    double key = sourceKey + 24.0;
    if (!isDrum) {
      key += track.transpose;
    }
    // Unlike the channel transpose command, loop transpose also applies while
    // percussion is active. In that mode it intentionally selects another key
    // in the drum kit, and therefore potentially another sample.
    key += track.loopTranspose[0] / 32 + track.loopTranspose[1] / 32;
    key = std::clamp(key, 0.0, 127.0);

    if (track.durationTieCanceled && track.previousTied) {
      track.previousTied = false;
      if (track.durationRate != 100 || isDrum) {
        out.expression(1.0);
      }
    }

    const bool tied = !isDrum && track.previousTied && track.previousKey && std::abs(*track.previousKey - key) < 0.001;
    double noteGain = gain;
    if (!isDrum && (track.previousTied || track.durationRate == 100)) {
      // A 100% duration is the driver's tie mode. Velocity remains live while
      // the voice is tied, so represent it as expression and keep the note
      // itself at full velocity.
      out.expression(gain);
      noteGain = 1.0;
    }

    PerformanceNoteId note;
    if (!isDrum && track.portamentoTime != 0 && track.previousKey && track.previousNote.valid() &&
        std::abs(*track.previousKey - key) >= 0.001) {
      note = out.note(key, noteGain, duration);
      PitchSlideOptions slide{
          .nativePortamento =
              NativePortamentoHint{
                  .timeMilliseconds = portamentoMilliseconds(track.portamentoTime, track.nmiRateHertz, track.tempo),
                  // F0 already retained the source driver's persistent glide
                  // setting at the command tick.
                  .emitTime = false,
              },
      };
      if (track.previousNoteStart + track.previousNoteDuration == vm.tick()) {
        slide.previousNote = track.previousNote;
        static_cast<void>(out.pitchSlide(note, *track.previousKey, key, track.portamentoTime, std::move(slide)));
      } else if (duration > 2) {
        static_cast<void>(
            out.at(vm.tick() + 1).pitchSlide(note, *track.previousKey, key, track.portamentoTime, std::move(slide)));
      }
    } else if (track.slideDuration != 0 && track.slideDepth != 0 && !isDrum &&
               duration > static_cast<u32>(track.slideDelay + 1)) {
      const double slideStartKey = std::clamp(key - track.slideDepth, 0.0, 127.0);
      note = out.note(key, noteGain, duration);
      static_cast<void>(out.at(vm.tick() + track.slideDelay)
                            .pitchSlide(note, slideStartKey, key, track.slideDuration,
                                        PitchSlideOptions{
                                            .nativePortamento =
                                                NativePortamentoHint{
                                                    .timeMilliseconds = portamentoMilliseconds(
                                                        track.slideDuration, track.nmiRateHertz, track.tempo),
                                                },
                                        }));
    } else {
      note = out.note(key, noteGain, duration, tied);
    }

    if (track.previousTied && (track.durationRate != 100 || isDrum)) {
      out.at(vm.tick() + duration).expression(1.0);
    }
    track.previousNoteStart = vm.tick();
    track.previousNoteDuration = duration;
    track.previousNoteGain = noteGain;
    track.previousKey = key;
    track.previousNote = note;
    track.previousTied = track.durationRate == 100 && !isDrum;
    track.durationTieCanceled = false;
  }

  void hold(u8 delta, u8 rate) {
    const u32 extension = static_cast<u32>(delta) * rate / 100;
    if (extension != 0 && track.previousKey) {
      track.previousNote = out.note(*track.previousKey, 1.0, extension, true);
    }
    track.durationTieCanceled = true;
  }

  void pan(u8 raw) {
    track.pan = raw;
    track.panMotion.setCurrent(raw);
    const auto [left, right] = stereoGains(raw | 0x10);
    out.stereoBalance(left, right);
  }

  void volume(u8 raw) {
    track.volumeMotion.setCurrent(raw);
    out.level(LevelScale::linearFromLinear(volumeGain(raw)), LevelPrecisionHint::FourteenBit);
  }

  void tempo(u8 raw, double nmiRate) {
    track.tempo = raw;
    track.nmiRateHertz = nmiRate;
    track.tempoMotion.setCurrent(raw);
    out.tempo(tempoMicrosecondsPerQuarter(nmiRate, raw));
  }

  void beginSlide(u8 kind, u8 duration, u8 target, double nmiRate) {
    auto* motion = kind == 0 ? &track.tempoMotion : kind == 1 ? &track.volumeMotion : &track.panMotion;
    PerformanceAutomationTarget automationTarget = PerformanceAutomationTarget::Pan;
    double targetValue = (static_cast<double>(panIndex(target)) - 7.0) / 7.0;
    if (kind == 0) {
      track.nmiRateHertz = nmiRate;
      automationTarget = PerformanceAutomationTarget::Tempo;
      targetValue = tempoMicrosecondsPerQuarter(nmiRate, target);
    } else if (kind == 1) {
      automationTarget = PerformanceAutomationTarget::Level;
      targetValue = volumeGain(target);
    }
    static_cast<void>(
        motion->begin(out.fade(automationTarget, targetValue, duration),
                      SequenceMotionPlan<double>::targetOverTicks(static_cast<double>(target), duration)));
  }

  void portamento(u8 raw) {
    track.portamentoTime = raw;
    track.slideDuration = 0;
    track.slideDelay = 0;
    if (raw != 0) {
      out.pitchTransitionSettings(portamentoMilliseconds(raw, track.nmiRateHertz, track.tempo));
    }
  }

  void slideMode(u8 delay, u8 duration, s8 depth) {
    track.slideDelay = delay == 0 ? 1 : delay;
    track.slideDuration = duration;
    track.slideDepth = depth;
  }

  void pitchBend(s8 raw) {
    track.pitchMotion.setCurrent(raw / 64.0);
    out.pitchBendRange(
        static_cast<u8>(std::clamp<int>(std::max(2, static_cast<int>(std::ceil(std::abs(raw / 64.0)))), 2, 127)));
    out.pitchBend(raw / 64.0);
  }

  void pitchSlide(u8 delay, u8 duration, u8 target) {
    delay = delay == 0 ? 1 : delay;
    if (!track.previousKey || !track.previousNote.valid() || duration == 0 || delay >= track.previousNoteDuration) {
      return;
    }
    // F3's operand is already the driver's absolute destination note. Unlike
    // ordinary note commands, the legacy driver does not apply channel or
    // loop transpose to this target.
    const double destination = target + 24.0;
    const u64 slideStart = track.previousNoteStart + delay;
    static_cast<void>(
        out.at(slideStart)
            .pitchSlide(
                track.previousNote, *track.previousKey, destination, duration,
                PitchSlideOptions{
                    .nativePortamento =
                        NativePortamentoHint{
                            .timeMilliseconds = portamentoMilliseconds(duration, track.nmiRateHertz, track.tempo),
                            .restoreTimeMilliseconds =
                                track.portamentoTime == 0 ? std::nullopt
                                                          : std::optional<double>{portamentoMilliseconds(
                                                                track.portamentoTime, track.nmiRateHertz, track.tempo)},
                        },
                }));
  }

  [[nodiscard]] Effects loopEnd(u8 slot, u8 totalPlays, s8 attenuationDelta, s8 transposeDelta) {
    const Address destination = track.loopStart[slot];
    if (destination.value == 0) {
      return {};
    }
    if (totalPlays == 0) {
      return Effects{.step = vm.declaredLoop(destination)};
    }

    Effects effects = vm.countedRepeatUntil(slot, totalPlays, destination);
    if (effects.step.kind == StepKind::Next) {
      track.loopAttenuation[slot] = 0;
      track.loopTranspose[slot] = 0;
    } else {
      track.loopAttenuation[slot] += attenuationDelta;
      track.loopTranspose[slot] += transposeDelta;
    }
    return effects;
  }

  void beginSubroutine(Address start) {
    track.subroutineStart = start;
    track.definingSubroutine = true;
  }

  [[nodiscard]] Effects subroutineBoundary(Address next) {
    if (track.definingSubroutine) {
      track.definingSubroutine = false;
      return {};
    }
    if (track.inSubroutine) {
      track.inSubroutine = false;
      return Effects{.step = vm.finiteBranch(track.subroutineReturn)};
    }
    if (track.subroutineStart.value == 0) {
      return {};
    }
    track.inSubroutine = true;
    track.subroutineReturn = next;
    return Effects{.step = vm.finiteBranch(track.subroutineStart)};
  }

  [[nodiscard]] Effects call(Address destination) {
    ++track.callDepth;
    return Effects{.step = vm.call(destination)};
  }

  [[nodiscard]] Effects returnOrEnd() {
    if (track.callDepth != 0) {
      --track.callDepth;
      return Effects{.step = vm.return_()};
    }
    return Effects{.step = vm.end()};
  }

  void tick() {
    const auto volumeTick = track.volumeMotion.tick();
    if (volumeTick.changed) {
      track.volumeMotion.output(out).level(LevelScale::linearFromLinear(volumeGain(static_cast<u8>(
                                               std::clamp<double>(track.volumeMotion.current(), 0.0, 255.0)))),
                                           LevelPrecisionHint::FourteenBit);
    }
    const auto panTick = track.panMotion.tick();
    if (panTick.changed) {
      const auto [left, right] =
          stereoGains(static_cast<u8>(std::clamp<double>(track.panMotion.current(), 0.0, 255.0)) | 0x10);
      track.panMotion.output(out).stereoBalance(left, right);
    }
    const auto tempoTick = track.tempoMotion.tick();
    if (tempoTick.changed) {
      track.tempo = track.tempoMotion.current();
      track.tempoMotion.output(out).tempo(tempoMicrosecondsPerQuarter(track.nmiRateHertz, track.tempo));
    }
    const auto pitchTick = track.pitchMotion.tick();
    if (pitchTick.changed) {
      out.pitchBend(track.pitchMotion.current());
    }
  }
};

using KonamiArcadeCursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address readDestination(KonamiArcadeCursor::Event& event, const KonamiArcadeLayout& layout,
                                      const KonamiArcadeSequenceLayout& sequence, SemanticOperandRole role) {
  if (layout.version == KonamiArcadeVersion::MysticWarrior) {
    const auto encoded = event.rawU16le("encoded_destination", SourceValueDisplay::Address);
    const u64 destination = encoded.value >= sequence.memoryBase
                                ? static_cast<u64>(sequence.offset) + encoded.value - sequence.memoryBase
                                : 0;
    return event.resolvedValue("destination", encoded, Address{destination}, SourceValueDisplay::Address, role);
  }
  const auto encoded = event.rawU32be("encoded_destination", SourceValueDisplay::Address);
  const u64 destination = static_cast<u64>(layout.code.offset) + encoded.value;
  return event.resolvedValue("destination", encoded, Address{destination}, SourceValueDisplay::Address, role);
}

[[nodiscard]] DecodedBytecodeCommand ignored(KonamiArcadeCursor& cursor, std::string_view label, u8 bytes) {
  auto event = cursor.sourceOnly(label);
  for (u8 index = 0; index < bytes; ++index) {
    event.u8("data_" + std::to_string(index + 1), SourceValueDisplay::Hex);
  }
  return event.ignore();
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, const KonamiArcadeLayout& layout,
                                                   const KonamiArcadeSequenceLayout& sequence,
                                                   std::array<Address, 2>& discoveredLoops,
                                                   Address& discoveredSubroutine,
                                                   std::vector<Diagnostic>* diagnostics) {
  KonamiArcadeCursor cursor(reader, begin, static_cast<u32>(layout.code.endOffset()), "konami-arcade", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  if (opcode == 0x60 || opcode == 0x61) {
    const bool enabled = opcode == 0x60;
    return cursor.command(enabled ? "Percussion On" : "Percussion Off", SequenceSemantic::Instrument)
        .invoke<&Playback::setPercussion>(u8{0}, enabled);
  }

  if (opcode < 0xc0) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    u8 key = 0;
    u8 delta = 0;
    if (opcode < 0x60) {
      key = event.opcodeValue("key", opcode, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      delta = event.u8("delta", SemanticOperandRole::Duration);
      event.set<&TrackState::previousDelta>(delta);
    } else {
      key = event.opcodeValue("key", static_cast<u8>(opcode - 0x62), SourceValueDisplay::MidiNote,
                              SemanticOperandRole::NoteKey);
    }
    const u8 durationOrVelocity = event.u8("duration_or_velocity", SemanticOperandRole::Duration);
    u8 velocity = durationOrVelocity;
    if (durationOrVelocity < 0x80) {
      event.set<&TrackState::durationRate>(durationOrVelocity);
      velocity = event.u8("velocity", SemanticOperandRole::Level);
    } else {
      velocity = durationOrVelocity - 0x80;
    }

    const KonamiArcadeDrum& drum = layout.drums[std::min<u8>(key, 45)];
    event.invoke<&Playback::note>(key, event.state<&TrackState::previousDelta>(), velocity, drum.defaultDuration,
                                  drum.pan);
    return event.wait(event.state<&TrackState::previousDelta>());
  }

  switch (opcode) {
    case 0xc0:
    case 0xc2:
    case 0xc3:
    case 0xcd:
      return ignored(cursor, "Unknown Driver State", 1);
    case 0xce:
      return ignored(cursor, "Unknown Driver State", 2);
    case 0xcf:
    case 0xd0:
      return ignored(cursor, "Unknown Driver State", 3);
    case 0xd1:
    case 0xd2:
    case 0xd3:
    case 0xd4:
    case 0xd5:
    case 0xd6:
      return ignored(cursor, opcode == 0xd2 ? "Reverb Volume" : "Unknown Driver State", 2);
    case 0xd7:
    case 0xd9:
      return ignored(cursor, "Unknown Driver State", 3);
    case 0xd8:
    case 0xda:
      return ignored(cursor, "Unknown Driver State", 2);
    case 0xde: {
      auto event = cursor.command("Percussion State", SequenceSemantic::Instrument);
      return event.invoke<&Playback::setPercussion>(u8{1}, event.u8("enabled", SemanticOperandRole::State) != 0);
    }
    case 0xdf:
      return ignored(cursor, "Unknown Driver State", 3);
    case 0xe0: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      event.set<&TrackState::previousDelta>(delta);
      event.set<&TrackState::durationTieCanceled>(true);
      return event.wait(delta);
    }
    case 0xe1: {
      auto event = cursor.command("Hold", SequenceSemantic::Note);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u8 rate = event.u8("duration_rate", SemanticOperandRole::Duration);
      event.set<&TrackState::previousDelta>(delta);
      event.invoke<&Playback::hold>(delta, rate);
      return event.wait(delta);
    }
    case 0xe2: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      return event.invoke<&Playback::programChange>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xe3: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xe4:
      return ignored(cursor, "Vibrato", 3);
    case 0xe5:
      return ignored(cursor, "Unknown Driver State", 3);
    case 0xe6:
    case 0xe8: {
      auto event = cursor.command(opcode == 0xe6 ? "Loop Start" : "Loop Start #2", SequenceSemantic::Loop);
      const u8 slot = event.derived("slot", static_cast<u8>(opcode == 0xe6 ? 0 : 1));
      const Address start = event.derived("loop_start", event.nextAddress(), SourceValueDisplay::Address,
                                          SemanticOperandRole::LoopTarget);
      discoveredLoops[slot] = start;
      return event.invoke([](Playback& playback, u8 runtimeSlot,
                             Address destination) { playback.track.loopStart[runtimeSlot] = destination; },
                          slot, start);
    }
    case 0xe7:
    case 0xe9: {
      auto event = cursor.command(opcode == 0xe7 ? "Loop End" : "Loop End #2", SequenceSemantic::Repeat);
      const u8 slot = event.derived("slot", static_cast<u8>(opcode == 0xe7 ? 0 : 1));
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const u8 encodedAttenuation = event.u8("attenuation_delta", SemanticOperandRole::Level);
      const s8 attenuation = static_cast<s8>(-static_cast<int>(encodedAttenuation));
      const s8 transpose = event.s8("transpose_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      if (discoveredLoops[slot].value != 0) {
        event.mayBranchTo(discoveredLoops[slot], SemanticOperandRole::RepeatTarget);
      }
      event.invoke<&Playback::loopEnd>(slot, count, attenuation, transpose);
      return event.runtimeControlFlow();
    }
    case 0xea: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 raw = event.u8("tempo");
      event.derived("microseconds_per_quarter", tempoMicrosecondsPerQuarter(layout.nmiRateHertz, raw));
      return event.invoke<&Playback::tempo>(raw, layout.nmiRateHertz);
    }
    case 0xeb: {
      auto event = cursor.command("Tempo Slide", SequenceSemantic::Tempo);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target");
      return event.invoke<&Playback::beginSlide>(u8{0}, duration, target, layout.nmiRateHertz);
    }
    case 0xec: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xed:
      return ignored(cursor, "Unknown Driver State", 3);
    case 0xee: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xef: {
      auto event = cursor.command("Volume Slide", SequenceSemantic::Level);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::beginSlide>(u8{1}, duration, target, 0.0);
    }
    case 0xf0: {
      auto event = cursor.command("Portamento", SequenceSemantic::Portamento);
      return event.invoke<&Playback::portamento>(event.u8("time", SemanticOperandRole::Duration));
    }
    case 0xf1: {
      auto event = cursor.command("Slide Mode", SequenceSemantic::Portamento);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const s8 depth = event.s8("depth", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      return event.invoke<&Playback::slideMode>(delay, duration, depth);
    }
    case 0xf2: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchBend>(
          event.s8("bend", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xf3: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Portamento);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target_note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      return event.invoke<&Playback::pitchSlide>(delay, duration, target);
    }
    case 0xf6: {
      auto event = cursor.command("Subroutine Definition", SequenceSemantic::State);
      discoveredSubroutine = event.nextAddress();
      return event.invoke<&Playback::beginSubroutine>(discoveredSubroutine);
    }
    case 0xf7: {
      auto event = cursor.command("Subroutine Boundary", SequenceSemantic::Call);
      if (discoveredSubroutine.value != 0) {
        event.mayBranchTo(discoveredSubroutine, SemanticOperandRole::CallTarget);
      }
      event.invoke<&Playback::subroutineBoundary>(event.nextAddress());
      return event.runtimeControlFlow();
    }
    case 0xf8: {
      auto event = cursor.command("Pan Slide", SequenceSemantic::Pan);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Pan);
      return event.invoke<&Playback::beginSlide>(u8{2}, duration, target, 0.0);
    }
    case 0xf9:
      return ignored(cursor, "Unknown Driver State", 1);
    case 0xfa: {
      auto event = cursor.command("Release Rate", SequenceSemantic::State);
      return event.set<&TrackState::releaseRate>(event.u8("rate"));
    }
    case 0xfd: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const Address destination = readDestination(event, layout, sequence, SemanticOperandRole::JumpTarget);
      return destination.value < begin ? event.loopCandidate(destination) : event.jump(destination);
    }
    case 0xfe: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      const Address destination = readDestination(event, layout, sequence, SemanticOperandRole::CallTarget);
      event.mayBranchTo(destination, SemanticOperandRole::CallTarget);
      event.invoke<&Playback::call>(destination);
      return event.runtimeControlFlow();
    }
    case 0xff: {
      auto event = cursor.command("Return / End", SequenceSemantic::End);
      event.invoke<&Playback::returnOrEnd>();
      return event.discoverReturn();
    }
    default:
      return cursor.unsupported("Unknown Opcode").stop();
  }
}

[[nodiscard]] SequenceDialect makeDialect() {
  return makeCompiledDialect<TrackState, Playback>(SequenceDialect{
      .id = DialectId{.value = std::string(kKonamiArcadeSequenceDialectId)},
      .commandDetailKindPrefix = "konami-arcade",
      .timebase = Timebase{.ppqn = kKonamiArcadePpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .commandLimit = kMaxTrackCommands,
              .initialLevel = 1.0,
              .initialReverbSend = 0.25,
          },
  });
}

}  // namespace

const SequenceDialect& konamiArcadeSequenceDialect() {
  static const SequenceDialect dialect = makeDialect();
  return dialect;
}

SequenceProgram decodeKonamiArcadeSequence(ByteReader reader, const KonamiArcadeLayout& layout,
                                           const KonamiArcadeSequenceLayout& sequenceLayout, AssetId sequenceAsset,
                                           SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const u32 pointerSize = layout.version == KonamiArcadeVersion::MysticWarrior ? 2 : 4;
  u32 trackCount = kKonamiArcadeMaxTracks;
  if (layout.version == KonamiArcadeVersion::MysticWarrior) {
    for (u32 index = 8; index < kKonamiArcadeMaxTracks; ++index) {
      const u32 pointer = sequenceLayout.offset + index * pointerSize;
      if (!reader.has(pointer, pointerSize) || reader.le16(pointer) == 0) {
        trackCount = 8;
        break;
      }
    }
  }
  while (trackCount != 0 && !reader.has(sequenceLayout.offset, static_cast<u64>(trackCount) * pointerSize)) {
    --trackCount;
  }

  const SourceRange headerRange = reader.range(sequenceLayout.offset, static_cast<u64>(trackCount) * pointerSize);
  SequenceDecodeSession sequence{
      reader,
      konamiArcadeSequenceDialect(),
      sequenceAsset,
      headerRange,
      sourceMap,
      kMaxTrackCommands,
      static_cast<u32>(layout.code.endOffset()),
  };

  for (u32 trackNumber = 0; trackNumber < trackCount; ++trackNumber) {
    const u32 pointerOffset = sequenceLayout.offset + trackNumber * pointerSize;
    u32 start = 0;
    u64 encoded = 0;
    if (layout.version == KonamiArcadeVersion::MysticWarrior) {
      encoded = reader.le16(pointerOffset);
      if (encoded >= sequenceLayout.memoryBase) {
        const u64 absolute = static_cast<u64>(sequenceLayout.offset) + encoded - sequenceLayout.memoryBase;
        if (absolute <= std::numeric_limits<u32>::max()) {
          start = static_cast<u32>(absolute);
        }
      }
    } else {
      encoded = reader.be32(pointerOffset);
      const u64 absolute = layout.code.offset + encoded;
      if (absolute <= std::numeric_limits<u32>::max()) {
        start = static_cast<u32>(absolute);
      }
    }
    if (encoded == 0 || start < layout.code.offset || !reader.has(start, 1)) {
      continue;
    }

    std::array<Address, 2> discoveredLoops;
    Address discoveredSubroutine;
    const auto decode = [&](u32 offset) {
      return decodeCommand(reader, offset, layout, sequenceLayout, discoveredLoops, discoveredSubroutine, diagnostics);
    };
    sequence.addLinearTrack(trackNumber, reader.range(pointerOffset, pointerSize), start, decode, encoded);
  }

  SequenceProgram program = sequence.finish();
  program.config.profile = static_cast<u32>(layout.version);
  return program;
}

}  // namespace vgmtrans::formats::konami_arcade

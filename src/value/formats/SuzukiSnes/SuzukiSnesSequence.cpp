/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiSnes/SuzukiSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace vgmtrans::formats::suzuki_snes {

using namespace core;

namespace {

constexpr std::array<u8, 13> kDurations{0xc0, 0x90, 0x60, 0x48, 0x30, 0x24, 0x20,
                                        0x18, 0x10, 0x0c, 0x08, 0x06, 0x03};

namespace math {

[[nodiscard]] u8 initialTempo(Version version) {
  return version == Version::SeikenDensetsu3 ? 0x60 : 0x3e;
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 timerTarget) {
  return timerTarget == 0 ? 60'000'000 : static_cast<u32>(kPpqn) * timerTarget * 125;
}

[[nodiscard]] double levelGain(u8 value) {
  return std::min(value / 128.0, 1.0);
}

[[nodiscard]] StereoBalance panGains(u8 value) {
  if (value < 0x80) {
    return StereoBalance{.leftGain = 1.0, .rightGain = value / 128.0};
  }
  if (value > 0x80) {
    return StereoBalance{.leftGain = (256 - value) / 128.0, .rightGain = 1.0};
  }
  return StereoBalance{};
}

[[nodiscard]] double panPosition(u8 value) {
  return value / 256.0;
}

[[nodiscard]] LfoPerformanceContext lfoContext(u8 period, u8 delay = 0) {
  const double cycles = period == 0 ? 0.0 : 1.0 / (4.0 * period);
  return LfoPerformanceContext{
      .cyclesPerTick = cycles,
      .delayTicks = delay,
      .delayIsTempoRelative = true,
      .waveform = LfoWaveform::Triangle,
      .initialPhaseCycles = 0.0,
  };
}

[[nodiscard]] double pitchLfoDepth(u8 period, s8 step) {
  // The driver adds a signed step*4 to an 8.8-semitone accumulator and
  // reverses direction after period ticks.
  return std::abs(static_cast<int>(step)) * period / 64.0;
}

}  // namespace math

struct RepeatInfo {
  u8 slot = 0;
  u32 totalPlays = 0;
  Address start;
  Address end;
};

struct TrackLayout {
  std::map<u32, RepeatInfo> starts;
  std::map<u32, RepeatInfo> ends;
  std::map<u32, RepeatInfo> breaks;
  std::optional<Address> loopPoint;
};

[[nodiscard]] bool repeatStart(Version version, u8 opcode) {
  return opcode == 0xd4 || (version == Version::SeikenDensetsu3 && (opcode == 0xd2 || opcode == 0xd3));
}

[[nodiscard]] u32 commandSize(Version version, ByteReader reader, u32 offset) {
  if (!reader.has(offset, 1)) {
    return 0;
  }
  const u8 opcode = reader.u8At(offset);
  if (opcode <= 0xc3) {
    return opcode / 14 == 13 ? 2 : 1;
  }
  switch (opcode) {
    case 0xc4:
    case 0xc5:
    case 0xc7:
    case 0xc9:
    case 0xca:
    case 0xcb:
    case 0xcc:
    case 0xd0:
    case 0xd5:
    case 0xd6:
    case 0xd7:
    case 0xd8:
    case 0xe6:
    case 0xee:
    case 0xef:
    case 0xf3:
    case 0xf7:
    case 0xf8:
    case 0xf9:
    case 0xfa:
    case 0xfb:
      return 1;
    case 0xc6:
    case 0xc8:
    case 0xcd:
    case 0xce:
    case 0xcf:
    case 0xd1:
    case 0xd2:
    case 0xd3:
    case 0xd4:
    case 0xd9:
    case 0xda:
    case 0xdb:
    case 0xdc:
    case 0xdd:
    case 0xde:
    case 0xdf:
    case 0xe0:
    case 0xe2:
    case 0xe3:
    case 0xe7:
    case 0xec:
    case 0xed:
    case 0xf2:
      return 2;
    case 0xe4:
    case 0xe5:
    case 0xe8:
    case 0xe9:
    case 0xf0:
    case 0xf4:
      return 3;
    case 0xf1:
    case 0xf5:
      return 4;
    case 0xe1:
    case 0xea:
    case 0xeb:
      return version == Version::SeikenDensetsu3 && opcode == 0xe1 ? 0 : 1;
    case 0xf6:
      return version == Version::SeikenDensetsu3 ? 1 : 2;
    case 0xfc:
      return version == Version::SuperMarioRpg ? 4 : (version == Version::SeikenDensetsu3 ? 2 : 1);
    case 0xfd:
      return version == Version::SeikenDensetsu3 ? 2 : 1;
    case 0xfe:
    case 0xff:
      return 1;
    default:
      return 0;
  }
}

[[nodiscard]] TrackLayout inspectTrack(ByteReader reader, Version version, u32 start) {
  struct OpenRepeat {
    u32 command = 0;
    RepeatInfo info;
    std::vector<u32> breaks;
  };
  TrackLayout layout;
  std::vector<OpenRepeat> stack;
  u32 offset = start;
  for (u32 commands = 0; commands < 32768 && reader.has(offset, 1); ++commands) {
    const u8 opcode = reader.u8At(offset);
    const u32 size = commandSize(version, reader, offset);
    if (size == 0 || !reader.has(offset, size)) {
      break;
    }
    const u32 next = offset + size;
    if (repeatStart(version, opcode) && stack.size() < 4) {
      const u8 count = reader.u8At(offset + 1);
      stack.push_back(OpenRepeat{
          .command = offset,
          .info = RepeatInfo{.slot = static_cast<u8>(stack.size()),
                             .totalPlays = count == 0 ? 256u : count,
                             .start = Address{next}},
      });
    } else if (opcode == 0xd6 && !stack.empty()) {
      stack.back().breaks.push_back(offset);
    } else if (opcode == 0xd5 && !stack.empty()) {
      OpenRepeat open = std::move(stack.back());
      stack.pop_back();
      open.info.end = Address{next};
      layout.starts.emplace(open.command, open.info);
      layout.ends.emplace(offset, open.info);
      for (const u32 breakAddress : open.breaks) {
        layout.breaks.emplace(breakAddress, open.info);
      }
    } else if (opcode == 0xd7) {
      layout.loopPoint = Address{next};
    }
    if (opcode == 0xd0 || opcode == 0xcd || opcode == 0xce ||
        (version == Version::SeikenDensetsu3 && (opcode == 0xfc || opcode == 0xfd))) {
      break;
    }
    offset = next;
  }
  return layout;
}

struct ProgramState {
  explicit ProgramState(const SequenceProgram& program)
      : version(static_cast<Version>(program.config.profile)), tempo(math::initialTempo(version)) {}

  Version version;
  u8 tempo = 0;
};

struct TrackState {
  explicit TrackState(const SequenceProgram& program)
      : version(static_cast<Version>(program.config.profile)) {
    volume.reset(0x3c);
    pan.reset(0x80);
  }

  Version version;
  s32 octave = 6;
  u8 durationRate = 0;
  u8 sourceProgram = 5;
  bool percussion = false;
  bool slur = false;
  bool initialized = false;
  bool previousWasRest = true;
  s8 fineTuning = 0;
  s16 transposeQuarters = 0;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  std::array<s32, 4> repeatOctaves{};
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> volume;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> pan;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() {
    if (!track.initialized) {
      track.initialized = true;
      out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.sourceProgram});
    }
  }

  [[nodiscard]] u32 soundingTicks(u32 length) const {
    if (track.durationRate == 0) {
      return length;
    }
    const u8 scale = static_cast<u8>((track.durationRate << 4) | (track.durationRate >> 4));
    return std::min(length, (length * scale) / 256 + 1);
  }

  [[nodiscard]] Effects note(u32 length, u8 scaleStep) {
    const double key = track.percussion ? scaleStep + kDrumKeyBias : track.octave * 12.0 + scaleStep;
    const u32 duration = soundingTicks(length);
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = 1.0,
        .durationTicks = duration,
        .restartsLfoPhase = !(track.slur && !track.previousWasRest),
    };
    if (track.slur && track.lastNote.valid() && !track.previousWasRest) {
      track.lastNote = track.lastKey && *track.lastKey == key
                           ? out.note(NotePerformanceEvent{.key = key,
                                                          .linearVelocity = 1.0,
                                                          .durationTicks = duration,
                                                          .extendsPrevious = true,
                                                          .restartsLfoPhase = false})
                           : out.continueVoice(track.lastNote, std::move(event));
    } else {
      track.lastNote = out.note(std::move(event));
    }
    track.lastKey = key;
    track.previousWasRest = false;
    return Effects::wait(length);
  }

  [[nodiscard]] Effects tie(u32 length) {
    if (track.lastKey && track.lastNote.valid() && !track.previousWasRest) {
      track.lastNote = out.note(NotePerformanceEvent{
          .key = *track.lastKey,
          .linearVelocity = 1.0,
          .durationTicks = soundingTicks(length),
          .extendsPrevious = true,
          .restartsLfoPhase = false,
      });
    }
    return Effects::wait(length);
  }

  [[nodiscard]] Effects rest(u32 length) {
    track.previousWasRest = true;
    track.lastNote = {};
    track.lastKey.reset();
    return Effects::wait(length);
  }

  void programChange(u8 value) {
    track.sourceProgram = value;
    if (!track.percussion) {
      out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = value});
    }
  }

  void percussion(bool enabled) {
    track.percussion = enabled;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain),
                                      .key = enabled ? kDrumKitKey : track.sourceProgram});
  }

  void tuning(s8 value) {
    track.fineTuning = value;
    emitTuning();
  }

  void transpose(s8 quarters) {
    track.transposeQuarters = quarters;
    emitTuning();
  }

  void transposeAdd(s8 quarters) {
    track.transposeQuarters = static_cast<s16>(track.transposeQuarters + quarters);
    emitTuning();
  }

  void emitTuning() { out.tuning(track.fineTuning * 6.25 + track.transposeQuarters * 25.0); }

  void tempo(u8 value) {
    program.tempo = value;
    out.tempo(math::tempoMicrosecondsPerQuarter(value));
  }

  void tempoAdd(s8 value) { tempo(static_cast<u8>(program.tempo + value)); }

  void volume(u8 value) {
    value &= 0x7f;
    track.volume.setCurrentAt(vm.tick(), value);
    out.level(math::levelGain(value), ValueQuantization{.levels = 128});
  }

  void volumeAdd(s8 delta) { volume(static_cast<u8>(track.volume.currentRaw() + delta)); }

  void volumeFade(u8 length, u8 target) {
    target &= 0x7f;
    if (length == 0) {
      volume(target);
      return;
    }
    static_cast<void>(track.volume.begin(out.fade(PerformanceAutomationTarget::Level, math::levelGain(target), length),
                                         SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
  }

  void emitPan(PerformanceEmitter output, u8 value) const {
    const StereoBalance gains = math::panGains(value);
    output.stereoBalance(gains.leftGain, gains.rightGain);
  }

  void pan(u8 value) {
    track.pan.setCurrentAt(vm.tick(), value);
    emitPan(out, value);
  }

  void panFade(u8 length, u8 target) {
    if (length == 0) {
      pan(target);
      return;
    }
    static_cast<void>(track.pan.begin(out.fade(PerformanceAutomationTarget::Pan, math::panPosition(target), length),
                                      SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
  }

  void vibrato(u8 period, s8 step, u8 delay) {
    const auto context = math::lfoContext(period, delay);
    out.vibratoDepth(math::pitchLfoDepth(period, step), context);
    out.vibratoRateCyclesPerTick(period == 0 ? 0.0 : 1.0 / (4.0 * period), context);
    out.vibratoDelayTicks(delay);
  }

  void vibratoOff() { out.vibratoDepth(0.0, math::lfoContext(0)); }

  void tremolo(u8 period, u8 step, u8 delay) {
    const auto context = math::lfoContext(period, delay);
    const double depth = std::min(1.0, step * period / 128.0);
    out.tremoloLinearGainDepth(depth, context);
    out.tremoloRateCyclesPerTick(period == 0 ? 0.0 : 1.0 / (4.0 * period), context);
    out.tremoloDelayTicks(delay);
  }

  void tremoloOff() { out.tremoloLinearGainDepth(0.0, math::lfoContext(0)); }

  void panLfo(u8 period, s8 step) {
    const auto context = math::lfoContext(period);
    out.panLfoDepth(std::min(1.0, std::abs(static_cast<int>(step)) * period / 128.0), context);
    out.panLfoRateCyclesPerTick(period == 0 ? 0.0 : 1.0 / (4.0 * period), context);
  }

  void panLfoOff() { out.panLfoDepth(0.0, math::lfoContext(0)); }

  void portamento(u8 rawLength, s8 semitones) {
    const u8 length = static_cast<u8>(rawLength - 1);
    if (length != 0 && track.lastKey) {
      static_cast<void>(out.fade(PerformanceAutomationTarget::Pitch, *track.lastKey + semitones, length));
    }
  }

  void beginRepeat(u8 slot) { track.repeatOctaves[slot] = track.octave; }

  [[nodiscard]] Effects endRepeat(u8 slot, u32 totalPlays, Address destination) {
    const Effects effects = vm.countedRepeatUntil(slot, totalPlays, destination);
    if (effects.flowOverride) {
      track.octave = track.repeatOctaves[slot];
    }
    return effects;
  }

  [[nodiscard]] Effects repeatBreak(u8 slot, Address destination) {
    return vm.countedRepeatBreak(slot, destination).effects;
  }

  void tick() {
    static_cast<void>(track.volume.tickRaw([&](s32 value) {
      track.volume.output(out).level(math::levelGain(static_cast<u8>(std::clamp<s32>(value, 0, 0x7f))),
                                     ValueQuantization{.levels = 128});
    }));
    static_cast<void>(track.pan.tickRaw([&](s32 value) {
      emitPan(track.pan.output(out), static_cast<u8>(std::clamp<s32>(value, 0, 0xff)));
    }));
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, Version version,
                                                   const TrackLayout& layout, std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "suzuki-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode <= 0xc3) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 durationIndex = event.opcodeValue("duration_index", static_cast<u8>(opcode / 14));
    const u8 note = event.opcodeValue("note_index", static_cast<u8>(opcode % 14), SourceValueDisplay::Default,
                                     SemanticOperandRole::NoteKey);
    u32 duration = durationIndex == 13 ? event.u8("duration", SemanticOperandRole::Duration)
                                      : kDurations[durationIndex];
    if (durationIndex == 13 && version == Version::SeikenDensetsu3) {
      ++duration;
      event.derived("effective_duration", duration, SemanticOperandRole::Duration);
    }
    if (note < 12) {
      return event.invoke<&Playback::note>(duration, note);
    }
    if (note == 12) {
      event.label("Rest");
      return event.invoke<&Playback::rest>(duration);
    }
    event.label("Tie");
    return event.invoke<&Playback::tie>(duration);
  }

  switch (opcode) {
    case 0xc4:
    case 0xf6:
    case 0xfe:
    case 0xff:
      if (version == Version::SeikenDensetsu3 || opcode == 0xc4 ||
          (version == Version::SuperMarioRpg && opcode == 0xff)) {
        return cursor.command("Octave Up", SequenceSemantic::Pitch).add<&TrackState::octave>(1);
      }
      if (opcode == 0xf6) {
        return cursor.ignored("Unknown F6", 1);
      }
      return cursor.sourceOnly("Unknown Command").ignore();
    case 0xc5:
      return cursor.command("Octave Down", SequenceSemantic::Pitch).add<&TrackState::octave>(-1);
    case 0xc6: {
      auto event = cursor.command("Set Octave", SequenceSemantic::Pitch);
      return event.set<&TrackState::octave>(event.u8("octave"));
    }
    case 0xc7:
      return cursor.noOp("No Operation", "nop");
    case 0xc8:
    case 0xdf:
      return cursor.ignored(opcode == 0xc8 ? "Noise Frequency" : "Relative Noise Frequency", 1, "noise");
    case 0xc9:
    case 0xca:
      return cursor.sourceOnly(opcode == 0xc9 ? "Noise On" : "Noise Off", "noise").ignore();
    case 0xcb:
    case 0xcc:
      return cursor.sourceOnly(opcode == 0xcb ? "Pitch Modulation On" : "Pitch Modulation Off", "pitch-modulation")
          .ignore();
    case 0xcd:
    case 0xce: {
      auto event = cursor.unsupported(opcode == 0xcd ? "Jump to Low SFX" : "Jump to High SFX", "sfx-jump");
      event.u8("effect");
      return event.stop();
    }
    case 0xcf: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      const auto raw = event.rawS8("raw");
      static_cast<void>(event.resolvedValue("cents", raw, raw.value * 6.25, SourceValueDisplay::Cents));
      return event.invoke<&Playback::tuning>(raw.value);
    }
    case 0xd0: {
      auto event = cursor.command("End", SequenceSemantic::End);
      return layout.loopPoint ? event.declaredLoop(*layout.loopPoint) : event.end();
    }
    case 0xd1: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const auto raw = event.rawU8("timer_target");
      static_cast<void>(event.resolvedValue("tempo", raw, tempoBeatsPerMinute(math::tempoMicrosecondsPerQuarter(raw.value)),
                                           SourceValueDisplay::BeatsPerMinute));
      return event.invoke<&Playback::tempo>(raw.value);
    }
    case 0xd2:
    case 0xd3:
      if (version != Version::SeikenDensetsu3) {
        return cursor.ignored(opcode == 0xd2 ? "Timer 1 Frequency" : "Relative Timer 1 Frequency", 1, "timer-1");
      }
      [[fallthrough]];
    case 0xd4: {
      auto event = cursor.command("Repeat Start", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const auto found = layout.starts.find(begin);
      if (found == layout.starts.end()) {
        return event.ignore();
      }
      event.derived("total_plays", count == 0 ? 256u : count, SemanticOperandRole::Count);
      return event.invoke<&Playback::beginRepeat>(found->second.slot);
    }
    case 0xd5: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      const auto found = layout.ends.find(begin);
      if (found == layout.ends.end()) {
        return event.ignore();
      }
      event.derived("destination", found->second.start, SourceValueDisplay::Address,
                    SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(found->second.start).runtimeControlFlow();
      return event.invoke<&Playback::endRepeat>(found->second.slot, found->second.totalPlays, found->second.start);
    }
    case 0xd6: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const auto found = layout.breaks.find(begin);
      if (found == layout.breaks.end()) {
        return event.ignore();
      }
      event.derived("destination", found->second.end, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      event.mayBranchTo(found->second.end).runtimeControlFlow();
      return event.invoke<&Playback::repeatBreak>(found->second.slot, found->second.end);
    }
    case 0xd7:
      return cursor.sourceOnly("Loop Point", "loop-point").ignore();
    case 0xd8:
      return cursor.command("Restore Instrument ADSR", SequenceSemantic::Envelope)
          .restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    case 0xd9: {
      auto event = cursor.command("Attack Rate", SequenceSemantic::Envelope);
      const u8 rate = event.u8("rate") & 0x0f;
      return event.emitEnvelopeField<EnvelopeFields::Attack>(snesDspAdsrAttackSeconds(rate),
                                                             VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
    case 0xda: {
      auto event = cursor.command("Decay Rate", SequenceSemantic::Envelope);
      const u8 rate = event.u8("rate") & 0x07;
      return event.emitEnvelopeField<EnvelopeFields::Decay>(snesDspAdsrDecaySeconds(rate),
                                                            VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
    case 0xdb: {
      auto event = cursor.command("Sustain Level", SequenceSemantic::Envelope);
      const u8 level = event.u8("level") & 0x07;
      return event.emitEnvelopeField<EnvelopeFields::Sustain>((level + 1) / 8.0,
                                                              VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
    case 0xdc: {
      auto event = cursor.command("Sustain Rate", SequenceSemantic::Envelope);
      const u8 rate = event.u8("rate") & 0x1f;
      return event.emitEnvelopeField<EnvelopeFields::SecondDecay>(snesDspAdsrSustainSeconds(rate),
                                                                  VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
    case 0xdd: {
      auto event = cursor.command("Duration Rate", SequenceSemantic::State);
      return event.set<&TrackState::durationRate>(event.u8("rate"));
    }
    case 0xde: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      return event.invoke<&Playback::programChange>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xe0:
      if (version != Version::SeikenDensetsu3) {
        return cursor.ignored("Unknown E0", 1);
      }
      [[fallthrough]];
    case 0xe2: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xe1:
    case 0xfc:
    case 0xfd:
      if (version != Version::SeikenDensetsu3 &&
          (opcode == 0xe1 || opcode == 0xfd ||
           (version == Version::BahamutLagoon && opcode == 0xfc))) {
        return cursor.command("Octave Up", SequenceSemantic::Pitch).add<&TrackState::octave>(1);
      }
      if (version == Version::SeikenDensetsu3 && (opcode == 0xfc || opcode == 0xfd)) {
        auto event = cursor.unsupported(opcode == 0xfc ? "Call Low SFX" : "Call High SFX", "sfx-call");
        event.u8("effect");
        return event.stop();
      }
      if (opcode == 0xfc && version == Version::SuperMarioRpg) {
        return cursor.ignored("Unknown FC", 3);
      }
      return cursor.unsupported("Undefined Command").stop();
    case 0xea:
      if (version == Version::SeikenDensetsu3) {
        return cursor.sourceOnly("Restart Pan LFO", "pan-lfo-restart").ignore();
      }
      return cursor.command("Octave Up", SequenceSemantic::Pitch).add<&TrackState::octave>(1);
    case 0xeb:
      if (version == Version::SeikenDensetsu3) {
        return cursor.command("Pan LFO Off", SequenceSemantic::Modulation).invoke<&Playback::panLfoOff>();
      }
      return cursor.command("Octave Up", SequenceSemantic::Pitch).add<&TrackState::octave>(1);
    case 0xe3: {
      auto event = cursor.command("Relative Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volumeAdd>(event.s8("delta"));
    }
    case 0xe4: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::volumeFade>(length, event.u8("target", SemanticOperandRole::Level));
    }
    case 0xe5: {
      auto event = cursor.command("Portamento", SequenceSemantic::Portamento);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::portamento>(length, event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0xe6:
      return cursor.sourceOnly("Portamento Toggle", "portamento-toggle").ignore();
    case 0xe7: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xe8: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::panFade>(length, event.u8("target", SemanticOperandRole::Pan));
    }
    case 0xe9: {
      auto event = cursor.command("Pan LFO On", SequenceSemantic::Modulation);
      const u8 period = event.u8("period");
      return event.invoke<&Playback::panLfo>(period, event.s8("step", SemanticOperandRole::Modulation));
    }
    case 0xec: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transpose>(event.s8("quarter_semitones", SemanticOperandRole::Pitch));
    }
    case 0xed: {
      auto event = cursor.command("Relative Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transposeAdd>(event.s8("quarter_semitones", SemanticOperandRole::Pitch));
    }
    case 0xee:
    case 0xef: {
      auto event = cursor.command(opcode == 0xee ? "Percussion On" : "Percussion Off", SequenceSemantic::Instrument);
      return event.invoke<&Playback::percussion>(opcode == 0xee);
    }
    case 0xf0:
    case 0xf1: {
      auto event = cursor.command("Vibrato On", SequenceSemantic::Modulation);
      const u8 period = event.u8("period");
      const s8 step = event.s8("step", SemanticOperandRole::Modulation);
      const u8 delay = opcode == 0xf1 ? event.u8("delay", SemanticOperandRole::Duration) : 0;
      return event.invoke<&Playback::vibrato>(period, step, delay);
    }
    case 0xf2: {
      auto event = cursor.command("Relative Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempoAdd>(event.s8("delta"));
    }
    case 0xf3:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case 0xf4:
    case 0xf5: {
      auto event = cursor.command("Tremolo On", SequenceSemantic::Modulation);
      const u8 period = event.u8("period");
      const u8 step = event.u8("step", SemanticOperandRole::Modulation);
      const u8 delay = opcode == 0xf5 ? event.u8("delay", SemanticOperandRole::Duration) : 0;
      return event.invoke<&Playback::tremolo>(period, step, delay);
    }
    case 0xf7:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation).invoke<&Playback::tremoloOff>();
    case 0xf8:
    case 0xf9: {
      auto event = cursor.command(opcode == 0xf8 ? "Slur On" : "Slur Off", SequenceSemantic::State);
      event.set<&TrackState::slur>(opcode == 0xf8);
      return event.emitLegatoPedal(opcode == 0xf8);
    }
    case 0xfa:
    case 0xfb:
      return cursor.command(opcode == 0xfa ? "Echo On" : "Echo Off", SequenceSemantic::State)
          .emitReverb(opcode == 0xfa ? 40.0 / 127.0 : 0.0);
    default:
      return cursor.unsupported("Unsupported SuzukiSnes Command").stop();
  }
}

struct ParsedHeader {
  SourceRange range;
  u32 pointerTable = 0;
  SequenceRecipes recipes;
};

[[nodiscard]] ParsedHeader parseHeader(ByteReader reader, const Layout& layout) {
  ParsedHeader parsed;
  u32 drum = layout.version == Version::SeikenDensetsu3 ? layout.sequenceHeaderAddress + kTrackCount * 2
                                                         : layout.sequenceHeaderAddress;
  while (reader.has(drum, 5) && reader.u8At(drum) < 0x80 && parsed.recipes.drums.size() < 128) {
    parsed.recipes.drums.push_back(DrumSlot{
        .note = reader.u8At(drum),
        .sourceProgram = reader.u8At(drum + 1),
        .sourceKey = reader.u8At(drum + 2),
        .volume = reader.u8At(drum + 3),
        .pan = reader.u8At(drum + 4),
        .source = reader.range(drum, 5),
    });
    drum += 5;
  }
  ++drum;  // terminator
  parsed.pointerTable = layout.version == Version::SeikenDensetsu3 ? layout.sequenceHeaderAddress : drum;
  const u32 end = layout.version == Version::SeikenDensetsu3 ? drum : parsed.pointerTable + kTrackCount * 2;
  parsed.range = reader.range(layout.sequenceHeaderAddress, end - layout.sequenceHeaderAddress);
  return parsed;
}

}  // namespace

const SequenceDialect& sequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = "suzuki-snes"},
      .commandDetailKindPrefix = "suzuki-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .defaultBehavior = SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .initialLevel = math::levelGain(0x3c),
          .initialReverbSend = 0.0,
          .initialStereoBalance = StereoBalance{},
          .initialMonoModeChannels = 0,
      },
  });
  return dialect;
}

TrackProgram decodeSourceTrack(ByteReader reader, Version version, u32 trackNumber, u32 startAddress,
                               std::vector<Diagnostic>* diagnostics) {
  const TrackLayout layout = inspectTrack(reader, version, startAddress);
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = 32768};
  return tracks.linear(trackNumber, startAddress,
                       [&](u32 offset) { return decodeCommand(reader, offset, version, layout, diagnostics); });
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  ParsedHeader header = parseHeader(reader, layout);
  SequenceDecodeSession sequence{reader, sequenceDialect(), sequenceId, header.range, sourceMap, 32768};
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u32 pointer = header.pointerTable + track * 2;
    const u16 start = reader.le16(pointer);
    if (start == 0) {
      continue;
    }
    const TrackLayout trackLayout = inspectTrack(reader, layout.version, start);
    sequence.addLinearTrack(track, reader.range(pointer, 2), start, [&](u32 offset) {
      return decodeCommand(reader, offset, layout.version, trackLayout, diagnostics);
    });
  }
  SequenceProgram program = sequence.finish();
  program.config.profile = static_cast<u32>(layout.version);
  program.behavior.initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(math::initialTempo(layout.version));
  return SequenceParse{
      .program = std::move(program),
      .recipes = std::move(header.recipes),
      .headerRange = header.range,
  };
}

}  // namespace vgmtrans::formats::suzuki_snes

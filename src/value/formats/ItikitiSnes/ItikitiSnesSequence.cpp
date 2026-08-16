/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ItikitiSnes/ItikitiSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::itikiti_snes {

using namespace core;

namespace {

constexpr std::array<u8, 16> kMasterLengths{
    0xc0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20, 0x18, 0x12, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03,
};
constexpr std::array<u8, 7> kDefaultLengths{0xc0, 0x60, 0x48, 0x30, 0x24, 0x18, 0x0c};
constexpr std::array<u8, 8> kLfoSine{0x00, 0x32, 0x62, 0x8e, 0xb5, 0xd5, 0xed, 0xfb};
constexpr double kTimer0Hz = 8000.0 / 0x27;
constexpr u8 kDefaultMasterVolume = 0x18;

enum class LfoTarget {
  Pitch,
  Gain,
};

namespace math {

[[nodiscard]] u32 ticks(u8 value) {
  return value == 0 ? 256 : value;
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 tempo) {
  // Timer 0 runs at 8 kHz with target $27. The 8.8 tempo accumulator
  // overflows once per sequence tick.
  return tempo == 0 ? 60'000'000 : static_cast<u32>(59'904'000u / tempo);
}

[[nodiscard]] double channelGain(u8 volume, u8 channelVolume) {
  return (volume / 255.0) * (channelVolume / 255.0);
}

[[nodiscard]] double masterGain(u8 value) {
  return value / 255.0;
}

[[nodiscard]] StereoBalance panGains(u8 pan, bool alternateMixer = false) {
  const auto coefficient = [alternateMixer](u16 value) {
    return alternateMixer && value >= 0x80 ? 0.5 : value / 256.0;
  };
  return {.leftGain = coefficient(0x100 - pan), .rightGain = coefficient(pan)};
}

[[nodiscard]] double panPosition(u8 pan) {
  return pan / 127.5 - 1.0;
}

[[nodiscard]] double fineTuningCents(s8 raw) {
  return 1200.0 * std::log2(1.0 + raw / 2048.0);
}

[[nodiscard]] LfoPolarity lfoPolarity(u8 rawDepth) {
  if ((rawDepth & 0x80) != 0) {
    return LfoPolarity::Bipolar;
  }
  return (rawDepth & 0x40) != 0 ? LfoPolarity::Negative : LfoPolarity::Positive;
}

[[nodiscard]] double lfoInitialPhase(LfoPolarity polarity, bool startsNegative = false) {
  return polarity == LfoPolarity::Bipolar && startsNegative ? 0.5 : 0.0;
}

[[nodiscard]] ModulationRange vibratoRange(u8 depth, LfoPolarity polarity) {
  const double upward = 12.0 * std::log2(1.0 + depth / 128.0);
  const double downward = 12.0 * std::log2(1.0 - depth / 256.0);
  return {
      .minimum = polarity == LfoPolarity::Positive ? 0.0 : downward,
      .maximum = polarity == LfoPolarity::Negative ? 0.0 : upward,
  };
}

[[nodiscard]] double lfoFrequency(u8 interval, LfoPolarity polarity) {
  const double stepsPerCycle = polarity == LfoPolarity::Bipolar ? 32.0 : 16.0;
  return kTimer0Hz / (stepsPerCycle * ticks(interval));
}

[[nodiscard]] u8 lfoMagnitude(u8 depth, u32 quarterStep) {
  return quarterStep == 8 ? depth : static_cast<u8>(kLfoSine[quarterStep] * depth / 256u);
}

[[nodiscard]] double normalizedLfoSample(u8 magnitude, u8 depth, bool negative, LfoTarget target) {
  if (magnitude == 0 || depth == 0) {
    return 0.0;
  }
  if (target == LfoTarget::Gain) {
    const double value = magnitude / static_cast<double>(depth);
    return negative ? -value / 2.0 : value;
  }
  if (negative) {
    return -std::log2(1.0 - magnitude / 256.0) / std::log2(1.0 - depth / 256.0);
  }
  return std::log2(1.0 + magnitude / 128.0) / std::log2(1.0 + depth / 128.0);
}

[[nodiscard]] std::vector<double> lfoCycleSamples(u8 depth, LfoPolarity polarity, LfoTarget target) {
  const u32 period = polarity == LfoPolarity::Bipolar ? 32 : 16;
  std::vector<double> samples;
  samples.reserve(period);
  for (u32 step = 0; step < period; ++step) {
    const u32 halfStep = step % 16;
    const u32 quarterStep = halfStep <= 8 ? halfStep : 16 - halfStep;
    const bool negative = polarity == LfoPolarity::Negative || (polarity == LfoPolarity::Bipolar && step >= 16);
    samples.push_back(normalizedLfoSample(lfoMagnitude(depth, quarterStep), depth, negative, target));
  }
  return samples;
}

}  // namespace math

[[nodiscard]] u8 instrumentProgram(u8 raw, u8 group) {
  // The upper SRCN bank is relocated by $10 for each non-music group.
  return (raw & 0x20) != 0 ? static_cast<u8>(raw + group * 0x10u) : raw;
}

struct RepeatFrame {
  Address start;
  u16 remaining = 0;
};

struct RuntimeConfig {
  u8 echoDelay = 0;
};

struct ProgramState {
  ProgramState(const SequenceProgram&, const RuntimeConfig& config) {
    tempo.reset(0x80);
    echo.voiceMask = 0;
    echo.delayMilliseconds = (config.echoDelay & 0x0fu) * 16.0;
  }

  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> tempo;
  std::optional<u32> tempoTrack;
  ReverbPerformanceEvent echo;
  u8 masterVolume = kDefaultMasterVolume;
  bool muted = false;
  bool alternateMixer = false;
  bool globalLfo = false;
};

struct TrackState {
  TrackState(const SequenceProgram&, const TrackProgram& sourceTrack)
      : trackNumber(sourceTrack.sourceTrackNumber),
        voiceBit(static_cast<u8>(1u << std::min<u32>(sourceTrack.sourceTrackNumber, 7))) {
    volume.reset(0xff);
    pan.reset(0x80);
  }

  u32 trackNumber = 0;
  u8 voiceBit = 1;
  std::array<u8, 7> lengths = kDefaultLengths;
  u8 noteBase = 0;
  s8 transpose = 0;
  u8 channelVolume = 0xff;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> volume;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> pan;

  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  u8 portamento = 0;
  u8 slideLength = 0;
  s8 slideSemitones = 0;
  u8 randomRange = 0;

  u8 vibratoDelay = 0;
  u8 vibratoInterval = 0;
  u8 vibratoRawDepth = 0;
  u8 tremoloDelay = 0;
  u8 tremoloInterval = 0;
  u8 tremoloRawDepth = 0;

  std::array<RepeatFrame, 4> repeats;
  u8 repeatDepth = 0;
  u8 alternativeCounter = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void emitLevel(PerformanceEmitter output) const {
    output.level(math::channelGain(static_cast<u8>(track.volume.currentRaw()), track.channelVolume),
                 ValueQuantization{.levels = 256});
  }

  void emitPan(PerformanceEmitter output) const {
    const StereoBalance gains = math::panGains(static_cast<u8>(track.pan.currentRaw()), program.alternateMixer);
    output.stereoBalance(gains.leftGain, gains.rightGain);
  }

  [[nodiscard]] LfoPerformanceContext lfoContext(u8 delay, u8 interval, u8 rawDepth, LfoTarget target,
                                                 bool restart) const {
    const LfoPolarity polarity = math::lfoPolarity(rawDepth);
    const bool startsNegative = polarity == LfoPolarity::Bipolar && program.globalLfo && (rawDepth & 0x40) != 0;
    // The driver builds the waveform from the stepped quarter-sine table at
    // $16e2. Bit 7 alternates between upward and downward pitch movement; when
    // bit 7 is clear, bit 6 selects one direction only.
    //
    // An LFO command always moves the waveform back to its starting position.
    // Normally it also begins the LFO delay again. Global-LFO mode preserves
    // the delay already in progress, so only the waveform position is reset.
    return LfoPerformanceContext{
        .frequencyHz = math::lfoFrequency(interval, polarity),
        .delayTicks = delay,
        .delayIsTempoRelative = true,
        .shape =
            LfoShape{
            .waveform = LfoWaveform::Sine,
            .samples = math::lfoCycleSamples(rawDepth & 0x3f, polarity, target),
        },
        .polarity = polarity,
        .initialPhaseCycles = math::lfoInitialPhase(polarity, startsNegative),
        .noteRestartInitialPhaseCycles = 0.0,
        .sampleImmediatelyOnNote = true,
        .delayUpdateMode =
            program.globalLfo ? LfoDelayUpdateMode::FutureNotesOnly : LfoDelayUpdateMode::CurrentAndFutureNotes,
        .restartMode = !restart ? LfoRestartMode::None
                                : (program.globalLfo ? LfoRestartMode::Phase : LfoRestartMode::PhaseAndDelay),
        .phaseRunsAtZeroDepth = false,
        .tremoloGainMode = TremoloGainMode::BipolarAroundNominal,
    };
  }

  void setLengths(u8 first, u8 second) {
    // pcall $00 leaves the second source byte in A. The driver therefore
    // applies it to master lengths 0-7 before loading the first byte from $dc
    // for lengths 8-15.
    const std::array<u8, 2> masks{second, first};
    size_t output = 0;
    for (u32 byte = 0; byte < masks.size() && output < track.lengths.size(); ++byte) {
      for (u32 bit = 0; bit < 8 && output < track.lengths.size(); ++bit) {
        if ((masks[byte] & (0x80u >> bit)) != 0) {
          track.lengths[output++] = kMasterLengths[byte * 8 + bit];
        }
      }
    }
  }

  void customLengths(u8 a, u8 b, u8 c, u8 d, u8 e, u8 f, u8 g) { track.lengths = {a, b, c, d, e, f, g}; }

  [[nodiscard]] u32 noteLength(u8 index, u8 literal) const {
    return math::ticks(index == 7 ? literal : track.lengths[index]);
  }

  [[nodiscard]] double noteKey(s8 relative) const {
    // The driver saturates the note base before applying signed transpose;
    // its DSP pitch table then pins every internal key >= 96 to one pitch.
    const int base = std::min<int>(track.noteBase + relative, 127);
    return 24 + std::clamp<int>(base + track.transpose, 0, 95);
  }

  [[nodiscard]] Effects note(s8 relative, u8 lengthIndex, u8 literal) {
    const u32 length = noteLength(lengthIndex, literal);
    const double key = noteKey(relative);
    const bool glide = track.portamento != 0 && track.lastNote.valid() && track.lastKey;
    const PerformanceNoteId previous = track.lastNote;
    std::optional<double> previousKey = out.currentPitchTransitionKey(previous);
    if (!previousKey) {
      previousKey = track.lastKey;
    }
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = 1.0,
        .durationTicks = length > 2 ? length - 2 : length,
        .restartsEnvelope = !glide,
        .restartsLfoPhase = false,
        .restartsVibratoLfoPhase = !program.globalLfo,
        .restartsTremoloLfoPhase = !program.globalLfo,
    };

    if (glide) {
      static_cast<void>(out.setNoteEnd(previous, vm.tick()));
      if (std::abs(*previousKey - key) < 0.000001) {
        event.extendsPrevious = true;
        track.lastNote = out.note(std::move(event));
      } else {
        track.lastNote = out.note(std::move(event));
        out.pitchSlide(track.lastNote, *previousKey, key, math::ticks(track.portamento))
            .continueFrom(previous)
            .continueAcrossNotes()
            .preferPortamento();
      }
    } else {
      track.lastNote = out.note(std::move(event));
    }
    track.lastKey = key;

    if (track.slideSemitones != 0) {
      const double target = std::clamp(key + track.slideSemitones, 24.0, 119.0);
      out.pitchSlide(track.lastNote, glide && previousKey ? *previousKey : key, target, math::ticks(track.slideLength))
          .preferPitchBend();
      track.lastKey = target;
      track.slideSemitones = 0;
    }
    return Effects::wait(length);
  }

  [[nodiscard]] Effects tie(u8 lengthIndex, u8 literal) {
    const u32 length = noteLength(lengthIndex, literal);
    if (track.lastNote.valid()) {
      static_cast<void>(out.setNoteEnd(track.lastNote, vm.tick() + (length > 2 ? length - 2 : length)));
    }
    return Effects::wait(length);
  }

  [[nodiscard]] Effects rest(u8 lengthIndex, u8 literal) {
    track.lastNote = {};
    track.lastKey.reset();
    return Effects::wait(noteLength(lengthIndex, literal));
  }

  void masterVolume(u8 value) {
    program.masterVolume = value;
    out.masterLevel(program.muted ? 0.0 : math::masterGain(value));
  }

  void echoVolume(s8 value, u8 group) {
    if (group != 0) {
      return;
    }
    const double gain = std::clamp(value / 127.0, -1.0, 1.0);
    program.echo.leftGain = gain;
    program.echo.rightGain = gain;
    program.echo.send = std::abs(gain);
    out.reverb(program.echo);
  }

  void echoFeedback(s8 feedback, u8 selector) {
    // The shipped driver branches over both writes when selector is nonzero.
    if (selector != 0) {
      return;
    }
    program.echo.feedback = feedback / 128.0;
    program.echo.filterIndex = 0;
    out.reverb(program.echo);
  }

  void channelVolume(u8 value) {
    track.channelVolume = value;
    emitLevel(out);
  }

  void tempo(u8 value) {
    program.tempo.setCurrentAt(vm.tick(), value);
    program.tempoTrack.reset();
    out.tempo(math::tempoMicrosecondsPerQuarter(value));
  }

  void tempoFade(u8 length, u8 target) {
    if (length == 0) {
      tempo(target);
      return;
    }
    static_cast<void>(program.tempo.begin(
        out.fade(PerformanceAutomationTarget::Tempo, math::tempoMicrosecondsPerQuarter(target), length),
        SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
    program.tempoTrack = track.trackNumber;
  }

  void volume(u8 value) {
    track.volume.setCurrentAt(vm.tick(), value);
    emitLevel(out);
  }

  void volumeFade(u8 length, u8 target) {
    if (length == 0) {
      volume(target);
      return;
    }
    static_cast<void>(track.volume.begin(
        out.fade(PerformanceAutomationTarget::Level, math::channelGain(target, track.channelVolume), length),
        SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
  }

  void pan(u8 value) {
    track.pan.setCurrentAt(vm.tick(), value);
    emitPan(out);
  }

  void panFade(u8 length, u8 target) {
    if (length == 0) {
      pan(target);
      return;
    }
    static_cast<void>(track.pan.begin(out.fade(PerformanceAutomationTarget::Pan, math::panPosition(target), length),
                                      SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
  }

  void programChange(u8 value) {
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = value},
                   InstrumentEnvelopeMode::UseInstrumentEnvelope);
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
  }

  void transposeAdd(s8 value) {
    track.transpose = static_cast<s8>(std::clamp<int>(track.transpose + value, -128, 127));
  }

  void vibrato(u8 delay, u8 interval, u8 rawDepth) {
    track.vibratoDelay = delay;
    track.vibratoInterval = interval;
    track.vibratoRawDepth = rawDepth;
    const u8 depth = rawDepth & 0x3f;
    auto context = lfoContext(delay, interval, rawDepth, LfoTarget::Pitch, true);
    if (depth == 0 && program.globalLfo) {
      context.zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote;
    }
    const ModulationRange range = math::vibratoRange(depth, math::lfoPolarity(rawDepth));
    context.pitchRangeSemitones = range;
    out.vibratoDepth(std::max(std::abs(range.minimum), std::abs(range.maximum)), context);
    context.restartMode = LfoRestartMode::None;
    out.vibratoRate(*context.frequencyHz, context);
    out.vibratoDelay(VibratoDelayPerformanceEvent{
        .delayTicks = delay,
        .tempoRelative = true,
        .updateMode =
            program.globalLfo ? LfoDelayUpdateMode::FutureNotesOnly : LfoDelayUpdateMode::CurrentAndFutureNotes,
    });
  }

  void vibratoOff() {
    auto context =
        lfoContext(track.vibratoDelay, track.vibratoInterval, track.vibratoRawDepth, LfoTarget::Pitch, false);
    // Vibrato-off stops producing new pitch changes, but it does not undo the
    // pitch change currently being heard. The next note returns the pitch to
    // its unmodulated value. This matches $0a26 and the depth-zero path at $08c5.
    context.zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote;
    out.vibratoDepth(0.0, std::move(context));
  }

  void tremolo(u8 delay, u8 interval, u8 rawDepth) {
    track.tremoloDelay = delay;
    track.tremoloInterval = interval;
    track.tremoloRawDepth = rawDepth;
    auto context = lfoContext(delay, interval, rawDepth, LfoTarget::Gain, true);
    if ((rawDepth & 0x3f) == 0 && program.globalLfo) {
      context.zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote;
    }
    out.tremoloLinearGainDepth((rawDepth & 0x3f) / 128.0, context);
    context.restartMode = LfoRestartMode::None;
    out.tremoloRate(*context.frequencyHz, context);
    out.tremoloDelay(TremoloDelayPerformanceEvent{
        .delayTicks = delay,
        .tempoRelative = true,
        .updateMode =
            program.globalLfo ? LfoDelayUpdateMode::FutureNotesOnly : LfoDelayUpdateMode::CurrentAndFutureNotes,
    });
  }

  void tremoloOff() {
    auto context = lfoContext(track.tremoloDelay, track.tremoloInterval, track.tremoloRawDepth, LfoTarget::Gain, false);
    // Tremolo-off stops producing new volume changes, but it leaves the current
    // volume change in place until the next note. This matches $0a49.
    context.zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote;
    out.tremoloLinearGainDepth(0.0, std::move(context));
  }

  void panLfo(u8 halfPeriod, u8 excursion) {
    const u32 period = math::ticks(halfPeriod);
    LfoPerformanceContext context{
        .cyclesPerTick = 1.0 / (4.0 * period),
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.0,
        .restartMode = LfoRestartMode::Phase,
    };
    out.panLfoDepth(std::min(1.0, excursion / 128.0), context);
    out.panLfoRateCyclesPerTick(*context.cyclesPerTick, context);
  }

  void panLfoOff() { out.panLfoDepth(0.0, LfoPerformanceContext{.cyclesPerTick = 0.0}); }

  void echoEnabled(bool enabled) {
    const u8 mask = program.echo.voiceMask.value_or(0);
    program.echo.voiceMask =
        enabled ? static_cast<u8>(mask | track.voiceBit) : static_cast<u8>(mask & static_cast<u8>(~track.voiceBit));
    out.reverb(program.echo);
  }

  void portamento(u8 speed) {
    track.portamento = speed;
    out.portamentoEnable(speed != 0);
  }

  void special(u8 value) {
    if (value < 0x80) {
      track.randomRange = value;
      return;
    }
    if (value < 0x82) {
      program.muted = (value & 1) != 0;
      out.masterLevel(program.muted ? 0.0 : math::masterGain(program.masterVolume));
      return;
    }
    if ((value & 2) != 0) {
      program.alternateMixer = (value & 1) != 0;
      emitPan(out);
    } else {
      program.globalLfo = (value & 1) != 0;
    }
  }

  void pitchSlide(u8 length, s8 semitones) {
    track.slideLength = length;
    track.slideSemitones = semitones;
  }

  [[nodiscard]] Effects repeatStart(u8 count, Address start) {
    if (track.repeatDepth >= track.repeats.size()) {
      return vm.end();
    }
    track.repeats[track.repeatDepth++] = RepeatFrame{
        .start = start,
        .remaining = count == 0 ? u16{0} : static_cast<u16>(count + 1u),
    };
    track.alternativeCounter = 0;
    return {};
  }

  [[nodiscard]] Effects repeatEnd() {
    if (track.repeatDepth == 0) {
      return {};
    }
    RepeatFrame& repeat = track.repeats[track.repeatDepth - 1];
    if (repeat.remaining == 0) {
      return vm.declaredLoop(repeat.start);
    }
    if (--repeat.remaining != 0) {
      return vm.finiteBranch(repeat.start);
    }
    --track.repeatDepth;
    return {};
  }

  [[nodiscard]] Effects conditionalSignalJump(Address) {
    // $0158 is written by the main CPU, so an extracted sequence has no
    // deterministic branch value. Preserve both source paths and play the
    // driver's ordinary fallthrough case.
    return {};
  }

  [[nodiscard]] Effects repeatBreak(u8 count, Address destination) {
    return ++track.alternativeCounter == count ? vm.finiteBranch(destination) : Effects{};
  }

  void tick() {
    static_cast<void>(track.volume.tickRaw([&](s32) { emitLevel(track.volume.output(out)); }));
    static_cast<void>(track.pan.tickRaw([&](s32) { emitPan(track.pan.output(out)); }));
    if (program.tempoTrack == track.trackNumber) {
      static_cast<void>(program.tempo.tickRaw([&](s32 value) {
        program.tempo.output(out).tempo(
            math::tempoMicrosecondsPerQuarter(static_cast<u8>(std::clamp<s32>(value, 0, 0xff))));
      }));
      if (!program.tempo.active()) {
        program.tempoTrack.reset();
      }
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address relativeTarget(u32 base, u16 relative) {
  return Address{static_cast<u16>(base + relative)};
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 sequenceBase, u8 groupIndex,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   ReferencedPrograms* references = nullptr) {
  Cursor cursor(reader, begin, "itikiti-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode >= 0x30) {
    const u8 lengthIndex = opcode & 7;
    if (opcode >= 0xf8) {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      event.opcodeValue("length_index", lengthIndex);
      const u8 literal = lengthIndex == 7 ? event.u8("length", SemanticOperandRole::Duration) : 0;
      return event.invoke<&Playback::rest>(lengthIndex, literal);
    }
    if (opcode >= 0xf0) {
      auto event = cursor.command("Tie", SequenceSemantic::Note);
      event.opcodeValue("length_index", lengthIndex);
      const u8 literal = lengthIndex == 7 ? event.u8("length", SemanticOperandRole::Duration) : 0;
      return event.invoke<&Playback::tie>(lengthIndex, literal);
    }
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const s8 relative = static_cast<s8>((opcode >> 3) - 6);
    event.opcodeValue("relative_key", relative, SourceValueDisplay::Default, SemanticOperandRole::NoteKey);
    event.opcodeValue("length_index", lengthIndex);
    const u8 literal = lengthIndex == 7 ? event.u8("length", SemanticOperandRole::Duration) : 0;
    return event.invoke<&Playback::note>(relative, lengthIndex, literal);
  }

  switch (opcode) {
    case 0x00:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0x01: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x02: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::State);
      return event.invoke<&Playback::echoVolume>(event.s8("volume", SemanticOperandRole::Level), groupIndex);
    }
    case 0x03: {
      auto event = cursor.command("Channel Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::channelVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x04: {
      auto event = cursor.command("Echo Feedback", SequenceSemantic::State);
      const s8 feedback = event.s8("feedback");
      return event.invoke<&Playback::echoFeedback>(feedback, event.u8("selector"));
    }
    case 0x05:
      return cursor.sourceOnly("Set Unused Voice Flag", "unused-voice-flag");
    case 0x06: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempo>(event.u8("tempo"));
    }
    case 0x07: {
      auto event = cursor.command("Tempo Fade", SequenceSemantic::Tempo);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::tempoFade>(length, event.u8("target"));
    }
    case 0x08: {
      auto event = cursor.sourceOnly("DSP Noise Frequency (Driver Bug)", "noise-frequency");
      static_cast<void>(event.u8("clock", SourceValueDisplay::Hex));
      return event;
    }
    case 0x09: {
      auto event = cursor.command("Note Length Set", SequenceSemantic::State);
      const u8 first = event.u8("first_mask", SourceValueDisplay::Hex);
      return event.invoke<&Playback::setLengths>(first, event.u8("second_mask", SourceValueDisplay::Hex));
    }
    case 0x0a: {
      auto event = cursor.command("Custom Note Lengths", SequenceSemantic::State);
      const u8 a = event.u8("length_0", SemanticOperandRole::Duration);
      const u8 b = event.u8("length_1", SemanticOperandRole::Duration);
      const u8 c = event.u8("length_2", SemanticOperandRole::Duration);
      const u8 d = event.u8("length_3", SemanticOperandRole::Duration);
      const u8 e = event.u8("length_4", SemanticOperandRole::Duration);
      const u8 f = event.u8("length_5", SemanticOperandRole::Duration);
      return event.invoke<&Playback::customLengths>(a, b, c, d, e, f,
                                                    event.u8("length_6", SemanticOperandRole::Duration));
    }
    case 0x0b: {
      auto event = cursor.command("Note Number Base", SequenceSemantic::State);
      return event.set<&TrackState::noteBase>(
          event.u8("note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey));
    }
    case 0x0c: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x0d: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::volumeFade>(length, event.u8("target", SemanticOperandRole::Level));
    }
    case 0x0e: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0x0f: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::panFade>(length, event.u8("target", SemanticOperandRole::Pan));
    }
    case 0x10: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 raw = event.u8("program", SemanticOperandRole::InstrumentProgram);
      const u8 program = instrumentProgram(raw, groupIndex);
      event.derived("effective_program", static_cast<u64>(program), SourceValueDisplay::Default,
                    SemanticOperandRole::InstrumentProgram);
      if (references != nullptr) {
        references->programs.insert(program);
      }
      return event.invoke<&Playback::programChange>(program);
    }
    case 0x11: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      return event.emitTuning(math::fineTuningCents(event.s8("fraction", SemanticOperandRole::Pitch)));
    }
    case 0x12: {
      auto event = cursor.command("Attack Rate", SequenceSemantic::Envelope);
      return event.emitEnvelopeField<EnvelopeFields::Attack>(snesDspAdsrAttackSeconds(event.u8("rate") & 0x0f));
    }
    case 0x13: {
      auto event = cursor.command("Decay Rate", SequenceSemantic::Envelope);
      return event.emitEnvelopeField<EnvelopeFields::Decay>(snesDspAdsrDecaySeconds(event.u8("rate") & 0x07));
    }
    case 0x14: {
      auto event = cursor.command("Sustain Level", SequenceSemantic::Envelope);
      return event.emitEnvelopeField<EnvelopeFields::Sustain>(((event.u8("level") & 0x07) + 1) / 8.0);
    }
    case 0x15: {
      auto event = cursor.command("Sustain Rate", SequenceSemantic::Envelope);
      return event.emitEnvelopeField<EnvelopeFields::SecondDecay>(snesDspAdsrSustainSeconds(event.u8("rate") & 0x1f));
    }
    case 0x16:
      return cursor.command("Restore Instrument ADSR", SequenceSemantic::Envelope).restoreEnvelope(EnvelopeFields::All);
    case 0x17: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0x18: {
      auto event = cursor.command("Transpose Add", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transposeAdd>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0x19: {
      auto event = cursor.command("Vibrato On", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 interval = event.u8("interval", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::vibrato>(delay, interval,
                                              event.u8("depth_and_mode", SemanticOperandRole::Modulation));
    }
    case 0x1a:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case 0x1b: {
      auto event = cursor.command("Tremolo On", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 interval = event.u8("interval", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::tremolo>(delay, interval,
                                              event.u8("depth_and_mode", SemanticOperandRole::Modulation));
    }
    case 0x1c:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation).invoke<&Playback::tremoloOff>();
    case 0x1d: {
      auto event = cursor.command("Pan LFO On", SequenceSemantic::Modulation);
      const u8 halfPeriod = event.u8("half_period", SemanticOperandRole::Duration);
      return event.invoke<&Playback::panLfo>(halfPeriod, event.u8("excursion", SemanticOperandRole::Modulation));
    }
    case 0x1e:
      return cursor.command("Pan LFO Off", SequenceSemantic::Modulation).invoke<&Playback::panLfoOff>();
    case 0x1f:
    case 0x20:
      return cursor.sourceOnly(opcode == 0x1f ? "DSP Noise On" : "DSP Noise Off", "noise");
    case 0x21:
    case 0x22:
      return cursor.sourceOnly(opcode == 0x21 ? "DSP Pitch Modulation On" : "DSP Pitch Modulation Off",
                               "pitch-modulation");
    case 0x23:
    case 0x24:
      return cursor.command(opcode == 0x23 ? "Echo On" : "Echo Off", SequenceSemantic::State)
          .invoke<&Playback::echoEnabled>(opcode == 0x23);
    case 0x25: {
      auto event = cursor.command("Portamento On", SequenceSemantic::Portamento);
      return event.invoke<&Playback::portamento>(event.u8("ticks", SemanticOperandRole::Duration));
    }
    case 0x26:
      return cursor.command("Portamento Off", SequenceSemantic::Portamento).invoke<&Playback::portamento>(u8{0});
    case 0x27: {
      auto event = cursor.command("Special Control", SequenceSemantic::State);
      return event.invoke<&Playback::special>(event.u8("control", SourceValueDisplay::Hex));
    }
    case 0x28:
      return cursor.command("Note Randomization Off", SequenceSemantic::State).set<&TrackState::randomRange>(u8{0});
    case 0x29: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Portamento);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      return event.invoke<&Playback::pitchSlide>(length, event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0x2a: {
      auto event = cursor.command("Repeat Start", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      return event.invokeFlow<&Playback::repeatStart>(count, Address{static_cast<u16>(begin + 2)});
    }
    case 0x2b: {
      auto event = cursor.sourceOnly("Main CPU Output Cursor", "cpu-output-cursor");
      static_cast<void>(event.u16le("cursor", SourceValueDisplay::Address));
      return event;
    }
    case 0x2c: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const u16 relative = event.u16le("relative", SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      const Address destination = relativeTarget(sequenceBase, relative);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return event.loopCandidate(destination);
    }
    case 0x2d: {
      auto event = cursor.command("CPU-Signaled Jump", SequenceSemantic::Jump);
      const u16 relative = event.u16le("relative", SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      const Address destination = relativeTarget(sequenceBase, relative);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return event.invoke<&Playback::conditionalSignalJump>(destination).mayBranchTo(destination);
    }
    case 0x2e:
      return cursor.command("Repeat End", SequenceSemantic::Repeat).invokeFlow<&Playback::repeatEnd>();
    case 0x2f: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const u16 relative = event.u16le("relative", SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      const Address destination = relativeTarget(sequenceBase, relative);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::repeatBreak>(count, destination).mayBranchTo(destination);
    }
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config = SequenceProgramConfig{
      .commandDetailKindPrefix = "itikiti-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
              .initialLevel = 1.0,
              .initialMasterLevel = math::masterGain(kDefaultMasterVolume),
              .initialReverbSend = 0.0,
              .initialStereoBalance = math::panGains(0x80),
              .initialMonoModeChannels = 0,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x80),
          },
  };
  return config;
}

SequenceRuntime sequenceRuntime(u8 echoDelay) {
  return makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{.echoDelay = echoDelay});
}

TrackProgram decodeSourceTrack(ByteReader reader, u32 trackNumber, u32 startAddress, u32 sequenceBase, u8 groupIndex,
                               std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = kCommandLimit};
  return tracks.decode(trackNumber, startAddress, [&](u32 offset) {
    return decodeCommand(reader, offset, sequenceBase, groupIndex, diagnostics);
  });
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const u32 headerSize = 2 + layout.trackCount * 2u;
  const SourceRange header = reader.range(layout.sequenceHeaderAddress, headerSize);
  ReferencedPrograms references{{0}};
  SequenceDecodeSession sequence{reader, sequenceConfig(), sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  for (u32 track = 0; track < layout.trackCount; ++track) {
    const u32 pointer = layout.sequenceHeaderAddress + 2 + track * 2;
    const u16 relative = reader.le16(pointer);
    const u16 start = static_cast<u16>(layout.sequenceBaseAddress + relative);
    sequence.addTrack(
        track, reader.range(pointer, 2), start,
        [&](u32 offset) {
          return decodeCommand(reader, offset, layout.sequenceBaseAddress, layout.groupIndex, diagnostics, &references);
        },
        relative);
  }
  SequenceProgram program = sequence.finish(sequenceRuntime(layout.echoDelay));
  return SequenceParse{.program = std::move(program), .references = std::move(references), .headerRange = header};
}

}  // namespace vgmtrans::formats::itikiti_snes

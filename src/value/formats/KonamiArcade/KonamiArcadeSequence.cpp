/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiArcade/KonamiArcade.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceLfo.h"
#include "value/sequence/SequenceMotion.h"

#include <algorithm>
#include <array>
#include <cmath>
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

[[nodiscard]] double reverbGain(u8 firstNibble, u8 secondNibble, bool gx) {
  // GX reads the high digit first. The Z80 interpreter builds the same byte
  // in the opposite operand order.
  const u8 loudness =
      gx ? static_cast<u8>((firstNibble << 4) | secondNibble) : static_cast<u8>((secondNibble << 4) | firstNibble);
  return attenuationGain(static_cast<u8>(~loudness));
}

[[nodiscard]] u8 panIndex(u8 raw) {
  if (raw >= 1 && raw <= 0x0f) {
    return raw - 1;
  }
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

[[nodiscard]] u8 effectiveTempo(u8 raw, s8 offset, KonamiArcadeVersion version) {
  const int value = static_cast<int>(raw) + offset;
  // The Z80 driver saturates positive overflow. GX uses an ordinary byte add.
  if (version == KonamiArcadeVersion::MysticWarrior && offset >= 0 && value > 0xff) {
    return 0xff;
  }
  return static_cast<u8>(value);
}

[[nodiscard]] double mysticPitchBendSemitones(s8 raw) {
  if (raw == 0) {
    return 0.0;
  }
  const u8 encoded = static_cast<u8>(raw);
  if (raw > 0) {
    const int coarse = (encoded & 0x40) != 0 ? 1 : 0;
    const int fraction = (encoded >> 2) & 0x0f;
    return coarse + fraction / 16.0;
  }

  // The negative path is not a plain arithmetic shift: after two SRA
  // instructions the driver complements carry and subtracts it, then forces
  // the result into a signed fractional nibble.
  int shifted = static_cast<int>(std::floor(raw / 4.0));
  if ((encoded & 0x02) == 0) {
    --shifted;
  }
  const int fraction = ((shifted % 16) + 16) % 16 - 16;
  const int coarse = (encoded & 0x40) != 0 ? 0 : -1;
  return coarse + fraction / 16.0;
}

[[nodiscard]] double vibratoDepthSemitones(u8 targetDepth, s32 currentDepth) {
  // The driver retains fade depth as 8.8 fixed point, but its triangle-wave
  // multiply uses only the integer byte. The configured target selects one of
  // two deliberately discontinuous depth scales.
  const s32 targetFixed = static_cast<s32>(targetDepth) << 8;
  const auto integerDepth = static_cast<u8>(std::clamp(currentDepth, 0, targetFixed) >> 8);
  return targetDepth < 0x80 ? integerDepth / 32.0 : integerDepth / 8.0;
}

[[nodiscard]] double tremoloDepthDecibels(u8 depth) {
  // GX computes floor(depth * 128 / 256); the Z80 driver stores depth >> 1
  // directly. Both produce the same peak attenuation. One loudness step is
  // 36/64 dB. The shared bipolar LFO uses NoBoost center attenuation, so its
  // depth is half the driver's full nominal-to-trough attenuation.
  const int peakAttenuationSteps = (static_cast<int>(depth) * 128) >> 8;
  return (36.0 * peakAttenuationSteps / 64.0) / 2.0;
}

struct VibratoState {
  void configure(u8 delayValue, u8 rateValue, u8 depthValue, bool continuousMode) {
    delay = delayValue;
    rate = rateValue;
    depth = depthValue;
    continuous = continuousMode && enabled();
    depthState.resetDepth(static_cast<s32>(depth) << 8);
  }

  [[nodiscard]] bool enabled() const { return rate != 0 && depth != 0; }
  [[nodiscard]] s32 targetDepthFixed() const { return depthState.targetDepth(); }
  [[nodiscard]] s32 currentDepthFixed() const { return depthState.currentDepth(); }

  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
  bool continuous = false;
  SequenceLfoDepthFadeState depthState;
};

struct TrackState {
  explicit TrackState(const SequenceProgram& program)
      : version(static_cast<KonamiArcadeVersion>(program.config.profile)) {
    panMotion.setCurrent(8.0);
  }

  KonamiArcadeVersion version = KonamiArcadeVersion::MysticWarrior;
  bool percussionFlag1 = false;
  bool percussionFlag2 = false;
  u8 previousDelta = 0;
  u8 previousDurationParameter = 0;
  u8 driverDurationRate = 0;
  u8 releaseRate = 0;
  u8 program = 0;
  s32 transpose = 0;
  std::array<Address, 2> loopStart;
  std::array<s16, 2> loopAttenuation{};
  std::array<s16, 2> loopTranspose{};
  double tempo = 120.0;
  double nmiRateHertz = 0.0;
  PerformanceBoundMotion<SequenceAutomatedValue<double>> volumeMotion;
  PerformanceBoundMotion<SequenceAutomatedValue<double>> panMotion;
  PerformanceBoundMotion<SequenceAutomatedValue<double>> tempoMotion;
  double pitchBendSemitones = 0.0;
  std::optional<double> emittedPitchBend;
  std::optional<double> emittedTuningCents;
  std::optional<double> previousKey;
  PerformanceNoteId previousNote;
  u64 previousNoteStart = 0;
  u32 previousNoteDuration = 0;
  bool previousTied = false;
  bool durationTieCanceled = false;
  u8 portamentoTime = 0;
  u8 slideDelay = 0;
  u8 slideDuration = 0;
  s8 slideDepth = 0;
  VibratoState vibrato;
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

  [[nodiscard]] bool isGx() const { return track.version == KonamiArcadeVersion::Gx; }
  [[nodiscard]] bool percussion() const { return track.percussionFlag1 || track.percussionFlag2; }
  [[nodiscard]] double driverMilliseconds(u8 ticks) const {
    return portamentoMilliseconds(ticks, track.nmiRateHertz, track.tempo);
  }
  [[nodiscard]] PitchSlideTiming slideTiming(u8 ticks) const {
    return PitchSlideTiming::fixedDuration(ticks, driverMilliseconds(ticks));
  }
  [[nodiscard]] LfoPerformanceContext vibratoContext() const {
    const auto& vibrato = track.vibrato;
    return LfoPerformanceContext{
        .cyclesPerTick = vibrato.enabled() ? std::optional<double>{vibrato.rate / 256.0} : std::optional<double>{0.0},
        // E4's delay counter compares before incrementing, so the first
        // nonzero sample occurs on music tick delay+1.
        .delayTicks = static_cast<u32>(vibrato.delay) + 1,
        .delayIsTempoRelative = true,
        .waveform = LfoWaveform::Triangle,
        // The shared simulator samples before advancing. Starting one phase
        // step ahead reproduces the driver's add-rate-then-sample order.
        .initialPhaseCycles = vibrato.rate / 256.0,
        .phaseRunsAtZeroDepth = vibrato.enabled(),
    };
  }

  [[nodiscard]] static LfoPerformanceContext tremoloContext(u8 delay, u8 rate, bool active) {
    return LfoPerformanceContext{
        .cyclesPerTick = active ? std::optional<double>{rate / 256.0} : std::optional<double>{0.0},
        // ED begins advancing between music ticks once its delay counter has
        // reached delay. At sequence-tick resolution, the first changed
        // sample is therefore observed at delay+1.
        .delayTicks = static_cast<u32>(delay) + 1,
        .delayIsTempoRelative = true,
        .waveform = LfoWaveform::Triangle,
        // The driver's absolute signed-byte phase starts at nominal gain and
        // advances before the first coarse tick sample.
        .initialPhaseCycles = std::fmod(0.25 + rate / 256.0, 1.0),
        .phaseRunsAtZeroDepth = false,
        .tremoloGainMode = TremoloGainMode::NoBoost,
    };
  }

  void emitVibratoDepth(PerformanceEmitter output, bool force = false) {
    auto& vibrato = track.vibrato;
    const double depth = vibrato.enabled() ? vibratoDepthSemitones(vibrato.depth, vibrato.currentDepthFixed()) : 0.0;
    vibrato.depthState.emitPhysicalDepth(
        depth, [&](double value) { output.vibratoDepth(value, vibratoContext()); }, force, 0.0001);
  }

  void configureVibrato(u8 delay, u8 rate, u8 depth, bool continuous) {
    auto& vibrato = track.vibrato;
    vibrato.configure(delay, rate, depth, continuous);

    const auto context = vibratoContext();
    emitVibratoDepth(out, true);
    if (vibrato.enabled()) {
      out.vibratoRateCyclesPerTick(vibrato.rate / 256.0, context);
      out.vibratoDelayTicks(static_cast<u32>(vibrato.delay) + 1);
    } else {
      out.vibratoRateCyclesPerTick(0.0, context);
      out.vibratoDelay(0, 0);
    }
  }

  void setVibratoFade(u8 ticks) { track.vibrato.depthState.configureLinearFade(ticks); }

  void configureTremolo(u8 delay, u8 rate, u8 depth) {
    const bool active = rate != 0 && depth != 0;
    const auto context = tremoloContext(delay, rate, active);
    out.tremoloDepth(active ? tremoloDepthDecibels(depth) : 0.0, context);
    if (active) {
      out.tremoloRateCyclesPerTick(rate / 256.0, context);
      out.tremoloDelayTicks(static_cast<u32>(delay) + 1);
    } else {
      out.tremoloRateCyclesPerTick(0.0, context);
      out.tremoloDelay(0, 0);
    }
  }

  [[nodiscard]] bool beginVibratoForNote(u8 durationRateAtNoteOn) {
    auto& vibrato = track.vibrato;
    const bool restarts = !vibrato.continuous && durationRateAtNoteOn < 0x65;
    if (!restarts) {
      return false;
    }

    vibrato.depthState.clearFadeAutomation();
    if (!vibrato.enabled() || !vibrato.depthState.restartFade(vibrato.delay)) {
      vibrato.depthState.resetCurrentDepth();
      return true;
    }

    const u32 onsetTick = static_cast<u32>(vibrato.delay) + 1;
    vibrato.depthState.bindFade(out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth,
                                                 vibratoDepthSemitones(vibrato.depth, vibrato.targetDepthFixed()),
                                                 vibrato.depthState.fadeDurationTicks(), onsetTick));
    emitVibratoDepth(vibrato.depthState.fadeOutput(out), true);
    return true;
  }

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

  void emitNotePitchState(bool isDrum, s8 initialTranspose) {
    // Drum selection and drum pitch are independent in the driver: the
    // source note chooses the table row, while channel/loop transpose and F2
    // tune that row's sample. Keep the kit key stable and express the latter
    // as tuning rather than selecting a different drum.
    const double tuningCents = isDrum ? (track.pitchBendSemitones + track.transpose + (isGx() ? 0 : initialTranspose) +
                                         track.loopTranspose[0] / 32.0 + track.loopTranspose[1] / 32.0) *
                                            100.0
                                      : 0.0;
    if (!track.emittedTuningCents || std::abs(*track.emittedTuningCents - tuningCents) > 0.0001) {
      if (track.emittedTuningCents || std::abs(tuningCents) > 0.0001) {
        out.tuning(tuningCents);
      }
      track.emittedTuningCents = tuningCents;
    }

    const double outputBend = isDrum ? 0.0 : track.pitchBendSemitones;
    if (!track.emittedPitchBend || std::abs(*track.emittedPitchBend - outputBend) > 0.0001) {
      if (track.emittedPitchBend || std::abs(outputBend) > 0.0001) {
        out.pitchBendRange(
            static_cast<u8>(std::clamp<int>(std::max(2, static_cast<int>(std::ceil(std::abs(outputBend)))), 2, 127)));
        out.pitchBend(outputBend);
      }
      track.emittedPitchBend = outputBend;
    }
  }

  void note(u8 sourceKey, u8 delta, u8 durationParameter, bool durationSpecified, u8 velocity, s8 initialAttenuation,
            s8 initialTranspose, u8 drumDuration, u8 drumPan) {
    const bool gx = isGx();
    const bool isDrum = percussion();
    bool usesDrumDefaultDuration = false;

    // The Z80 installs an explicit duration before drum setup, including
    // zero. GX does not install its separately retained duration parameter
    // until after set_note and its LFO-reset checks.
    if (durationSpecified && !gx) {
      track.driverDurationRate = durationParameter;
    }

    u8 durationRateAtNoteOn = track.driverDurationRate;
    if (isDrum) {
      if (gx) {
        // GX drum setup runs before the note-on/LFO-reset checks and always
        // installs the table duration, replacing zero with 99%.
        durationRateAtNoteOn = drumDuration == 0 ? 99 : drumDuration;
        track.driverDurationRate = durationRateAtNoteOn;
        usesDrumDefaultDuration = durationParameter == 0;
      } else if (track.driverDurationRate == 0) {
        // The Z80 driver only consults the drum table when the live duration
        // is zero and does not substitute for a zero table value.
        durationRateAtNoteOn = drumDuration;
        track.driverDurationRate = drumDuration;
        usesDrumDefaultDuration = drumDuration != 0;
      }
    }
    const bool restartsTremolo = durationRateAtNoteOn < 0x65;
    const bool restartsVibrato = beginVibratoForNote(durationRateAtNoteOn);
    if (gx && durationParameter != 0) {
      track.driverDurationRate = durationParameter;
    }
    const u8 durationRate = track.driverDurationRate;

    const int loopAttenuation = track.loopAttenuation[0] + track.loopAttenuation[1];
    // GX stores table byte 3 directly in its signed attenuation accumulator.
    // The Z80 loader negates table byte 4 into the same domain.
    const int headerAttenuation = gx ? initialAttenuation : -initialAttenuation;
    const int attenuation = std::clamp(127 - static_cast<int>(velocity) + loopAttenuation + headerAttenuation, 0, 127);
    const double gain = attenuationGain(attenuation);

    u32 duration = delta;
    if (usesDrumDefaultDuration && sourceKey < 46) {
      duration = std::max<u32>(1, static_cast<u32>(delta) * durationRate / 100);
      if (track.panMotion.current() == 0.0) {
        const auto [left, right] = stereoGains(drumPan);
        out.stereoBalance(left, right);
      }
    } else if (durationRate != 0 && track.releaseRate != 0) {
      duration = std::max<u32>(1, static_cast<u32>(delta) * durationRate / 100);
    }

    double key = sourceKey + 24.0;
    if (!isDrum) {
      key += initialTranspose + track.transpose;
      key += track.loopTranspose[0] / 32.0 + track.loopTranspose[1] / 32.0;
    }
    key = std::clamp(key, 0.0, 127.0);

    emitNotePitchState(isDrum, initialTranspose);

    if (track.durationTieCanceled && track.previousTied) {
      track.previousTied = false;
      if (durationRate < 100 || isDrum) {
        out.expression(1.0);
      }
    }

    const bool tied = !isDrum && track.previousTied && track.previousKey && std::abs(*track.previousKey - key) < 0.001;
    double noteGain = gain;
    if (!isDrum && (track.previousTied || durationRate >= 100)) {
      // A 100% duration is the driver's tie mode. Velocity remains live while
      // the voice is tied, so represent it as expression and keep the note
      // itself at full velocity.
      out.expression(gain);
      noteGain = 1.0;
    }

    const auto emitNote = [&](double noteKey, double linearVelocity, u32 noteDuration, bool extendsPrevious = false) {
      return out.note(NotePerformanceEvent{
          .key = noteKey,
          .linearVelocity = linearVelocity,
          .durationTicks = noteDuration,
          .extendsPrevious = extendsPrevious,
          .restartsLfoPhase = restartsVibrato,
          .restartsVibratoLfoPhase = restartsVibrato,
          .restartsTremoloLfoPhase = restartsTremolo,
      });
    };

    PerformanceNoteId note;
    if (!isDrum && track.portamentoTime != 0 && track.previousKey && track.previousNote.valid() &&
        std::abs(*track.previousKey - key) >= 0.001) {
      note = emitNote(key, noteGain, duration);
      if (track.previousNoteStart + track.previousNoteDuration == vm.tick()) {
        auto slide = out.pitchSlide(note, *track.previousKey, key, track.portamentoTime);
        slide.continueFrom(track.previousNote).useCurrentPortamentoTiming();
      } else if (duration > 2) {
        out.at(vm.tick() + 1)
            .pitchSlide(note, *track.previousKey, key, track.portamentoTime)
            .useCurrentPortamentoTiming();
      }
    } else if (track.slideDuration != 0 && track.slideDepth != 0 && !isDrum &&
               duration > static_cast<u32>(track.slideDelay + 1)) {
      const double slideStartKey = std::clamp(key - track.slideDepth, 0.0, 127.0);
      note = emitNote(key, noteGain, duration);
      out.at(vm.tick() + static_cast<u32>(track.slideDelay) + 1)
          .pitchSlide(note, slideStartKey, key, slideTiming(track.slideDuration));
    } else {
      note = emitNote(key, noteGain, duration, tied);
    }

    if (track.previousTied && (durationRate < 100 || isDrum)) {
      out.at(vm.tick() + duration).expression(1.0);
    }
    track.previousNoteStart = vm.tick();
    track.previousNoteDuration = duration;
    track.previousKey = key;
    track.previousNote = note;
    track.previousTied = durationRate >= 100 && !isDrum;
    track.durationTieCanceled = false;
  }

  void hold(u8 delta, u8 rate) {
    const u32 scaled = static_cast<u32>(delta) * rate / 100;
    const u32 extension = isGx() ? std::max<u32>(1, scaled) : scaled;
    if (extension != 0 && track.previousKey) {
      track.previousNote = out.note(*track.previousKey, 1.0, extension, true);
    }
    track.previousDurationParameter = rate;
    track.driverDurationRate = rate;
    track.durationTieCanceled = true;
  }

  void pan(u8 raw) {
    if (raw == 0) {
      // Zero disables sequence pan and lets a subsequent drum choose its pan;
      // it does not force the current voice to center.
      track.panMotion.clearMotion();
      track.panMotion.setCurrent(0.0);
      return;
    }
    track.panMotion.setCurrent(raw & 0x0f);
    const auto [left, right] = stereoGains(raw);
    out.stereoBalance(left, right);
  }

  void volume(u8 raw) {
    track.volumeMotion.setCurrent(raw);
    out.level(LevelScale::linearFromLinear(volumeGain(raw)), LevelPrecisionHint::FourteenBit);
  }

  void reverb(u8 firstNibble, u8 secondNibble) { out.reverb(reverbGain(firstNibble, secondNibble, isGx())); }

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
      out.pitchTransitionSettings(driverMilliseconds(raw));
    }
  }

  void slideMode(u8 delay, u8 duration, s8 depth) {
    track.slideDelay = delay;
    track.slideDuration = duration;
    track.slideDepth = depth;
  }

  void pitchBend(s8 raw) {
    track.pitchBendSemitones = isGx() ? raw / 64.0 : mysticPitchBendSemitones(raw);
    // F2 only updates the source pitch used by the next note calculation; it
    // does not retune an already playing voice.
  }

  void pitchSlide(u8 delay, u8 duration, u8 target, s8 initialTranspose) {
    const bool gx = isGx();
    if (!track.previousKey || !track.previousNote.valid() || (gx && duration == 0)) {
      return;
    }
    double destination;
    u64 slideStart;
    if (gx) {
      // GX's look-ahead path adds channel transpose (but not loop transpose)
      // and clamps to its 0..95 pitch domain.
      destination = std::clamp<int>(static_cast<int>(target) + track.transpose, 0, 95) + 24.0;
      // It recognizes F3 one music tick after note-on, after that tick's
      // effect update.
      slideStart = track.previousNoteStart + static_cast<u64>(delay) + 2;
    } else {
      // The Z80 driver consumes F3 as part of the preceding note-on and
      // evaluates its target through the same transpose path as that note.
      delay = delay == 0 ? 1 : delay;
      duration = static_cast<u8>(duration + 1);
      if (duration == 0) {
        return;
      }
      destination = target + 24.0 + initialTranspose + track.transpose + track.loopTranspose[0] / 32.0 +
                    track.loopTranspose[1] / 32.0;
      slideStart = track.previousNoteStart + delay;
    }
    if (slideStart >= track.previousNoteStart + track.previousNoteDuration) {
      return;
    }
    auto slide =
        out.at(slideStart).pitchSlide(track.previousNote, *track.previousKey, destination, slideTiming(duration));
    if (track.portamentoTime != 0) {
      slide.restorePortamentoTiming(driverMilliseconds(track.portamentoTime));
    }
  }

  [[nodiscard]] Effects loopEnd(u8 slot, u8 totalPlays, s16 attenuationDelta, s8 transposeDelta) {
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
    static_cast<void>(track.volumeMotion.tickChanged([&](double value) {
      track.volumeMotion.output(out).level(
          LevelScale::linearFromLinear(volumeGain(static_cast<u8>(std::clamp(value, 0.0, 255.0)))),
          LevelPrecisionHint::FourteenBit);
    }));
    static_cast<void>(track.panMotion.tickChanged([&](double value) {
      const auto [left, right] = stereoGains(static_cast<u8>(std::clamp(value, 0.0, 255.0)) | 0x10);
      track.panMotion.output(out).stereoBalance(left, right);
    }));
    static_cast<void>(track.tempoMotion.tickChanged([&](double value) {
      track.tempo = value;
      track.tempoMotion.output(out).tempo(tempoMicrosecondsPerQuarter(track.nmiRateHertz, track.tempo));
    }));
    const auto vibratoTick = track.vibrato.depthState.tickFade();
    if (vibratoTick.shouldApply()) {
      emitVibratoDepth(track.vibrato.depthState.fadeOutput(out));
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
  const bool gx = layout.version == KonamiArcadeVersion::Gx;
  const bool mystic = layout.version == KonamiArcadeVersion::MysticWarrior;
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
    const bool durationSpecified = durationOrVelocity < 0x80;
    u8 durationParameter = 0;
    u8 velocity = durationOrVelocity;
    if (durationSpecified) {
      durationParameter = durationOrVelocity;
      event.set<&TrackState::previousDurationParameter>(durationParameter);
      velocity = event.u8("velocity", SemanticOperandRole::Level);
    } else {
      velocity = durationOrVelocity - 0x80;
    }

    const KonamiArcadeDrum& drum = layout.drums[std::min<u8>(key, 45)];
    event.invoke<&Playback::note>(key, event.state<&TrackState::previousDelta>(),
                                  event.state<&TrackState::previousDurationParameter>(), durationSpecified, velocity,
                                  sequence.initialAttenuation, sequence.initialTranspose, drum.defaultDuration,
                                  drum.pan);
    return event.wait(event.state<&TrackState::previousDelta>());
  }

  switch (opcode) {
    case 0xc0:
    case 0xc1:
    case 0xc2:
    case 0xc3:
      return ignored(cursor, "Unknown Driver State", 1);
    case 0xc4:
    case 0xc5:
    case 0xc6:
      return ignored(cursor, gx ? "DSP Command" : "Unknown Driver State", gx ? 7 : 0);
    case 0xc7:
    case 0xc8:
    case 0xc9:
    case 0xca:
    case 0xcb:
    case 0xcc:
      return ignored(cursor, "Unknown Driver State", 0);
    case 0xcd:
      return ignored(cursor, "Unknown Driver State", gx ? 1 : 0);
    case 0xce:
      return ignored(cursor, "Unknown Driver State", gx ? 2 : 0);
    case 0xcf:
    case 0xd0:
      return ignored(cursor, "Unknown Driver State", 3);
    case 0xd1:
    case 0xd3:
    case 0xd4:
    case 0xd5:
    case 0xd6:
      return ignored(cursor, "Unknown Driver State", 2);
    case 0xd2: {
      auto event = cursor.command("Reverb Volume", SequenceSemantic::Level);
      const u8 first = event.u8("first_nibble", SemanticOperandRole::Level);
      const u8 second = event.u8("second_nibble", SemanticOperandRole::Level);
      event.derived("linear_gain", reverbGain(first, second, gx), SemanticOperandRole::Level);
      return event.invoke<&Playback::reverb>(first, second);
    }
    case 0xd7:
    case 0xd9:
      return ignored(cursor, "Unknown Driver State", 3);
    case 0xd8:
    case 0xda:
      return ignored(cursor, "Unknown Driver State", 2);
    case 0xdb:
      return ignored(cursor, "Unknown Driver State", 1);
    case 0xdc: {
      if (mystic) {
        return ignored(cursor, "Unknown Driver State", 0);
      }
      auto event = cursor.command("Sample Loop Program", SequenceSemantic::State);
      return event.set<&TrackState::program>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xdd:
      return ignored(cursor, "Unknown Driver State", 0);
    case 0xde: {
      auto event = cursor.command("Percussion State", SequenceSemantic::Instrument);
      return event.invoke<&Playback::setPercussion>(u8{1}, event.u8("enabled", SemanticOperandRole::State) != 0);
    }
    case 0xdf: {
      auto event = cursor.command("Continuous Vibrato", SequenceSemantic::Modulation);
      const u8 rawDelay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 rawRate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 delay = mystic && rawDelay == 0 ? 1 : rawDelay;
      const u8 rate = mystic ? static_cast<u8>(rawRate >> 1) : rawRate;
      event.derived("effective_delay", delay, SemanticOperandRole::Duration);
      event.derived("effective_rate", rate, SemanticOperandRole::Modulation);
      return event.invoke<&Playback::configureVibrato>(delay, rate, depth, true);
    }
    case 0xe0: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      event.set<&TrackState::previousDelta>(delta);
      if (gx) {
        // GX clears the live duration but retains the separately stored
        // duration parameter used by later velocity-only notes.
        event.set<&TrackState::driverDurationRate>(u8{0});
      }
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
    case 0xe4: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 rawDelay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 rawRate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 delay = mystic && rawDelay == 0 ? 1 : rawDelay;
      const u8 rate = mystic && rawRate == 0 ? 1 : rawRate;
      event.derived("effective_delay", delay, SemanticOperandRole::Duration);
      event.derived("effective_rate", rate, SemanticOperandRole::Modulation);
      return event.invoke<&Playback::configureVibrato>(delay, rate, depth, false);
    }
    case 0xe5: {
      // The driver adds rate to an 8-bit accumulator on every K054539 update.
      // On carry, it briefly adds the next masked word from its random table
      // to pitch; otherwise the offset is zero. This sub-tick impulse effect
      // has no faithful sequence-tick/MIDI representation yet, but retain its
      // decoded parameters instead of presenting it as unknown.
      auto event = cursor.sourceOnly("Random Pitch Spikes");
      static_cast<void>(event.u8("rate", SemanticOperandRole::Modulation));
      const u8 maskHigh = event.u8("mask_high", SourceValueDisplay::Hex, SemanticOperandRole::Modulation);
      const u8 maskLow = event.u8("mask_low", SourceValueDisplay::Hex, SemanticOperandRole::Modulation);
      const u16 mask = static_cast<u16>((static_cast<u16>(maskHigh) << 8) | maskLow);
      static_cast<void>(event.derived("maximum_offset_semitones", mask / 256.0, SemanticOperandRole::Pitch));
      return event.ignore();
    }
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
      const s8 loudnessDelta =
          event.s8("loudness_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      // The driver adds this signed byte to loudness. Convert it to the
      // attenuation-domain state used by the performance model.
      const s16 attenuation = -static_cast<s16>(loudnessDelta);
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
      const u8 effective = effectiveTempo(raw, sequence.tempoOffset, layout.version);
      event.derived("effective_tempo", effective);
      event.derived("microseconds_per_quarter", tempoMicrosecondsPerQuarter(layout.nmiRateHertz, effective));
      return event.invoke<&Playback::tempo>(effective, layout.nmiRateHertz);
    }
    case 0xeb: {
      auto event = cursor.command("Tempo Slide", SequenceSemantic::Tempo);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 rawTarget = event.u8("target");
      // GX applies the sequence tempo offset to both EA and EB. The Z80
      // driver applies it only to an immediate EA tempo command.
      const u8 target = gx ? effectiveTempo(rawTarget, sequence.tempoOffset, layout.version) : rawTarget;
      event.derived("effective_target", target);
      return event.invoke<&Playback::beginSlide>(u8{0}, duration, target, layout.nmiRateHertz);
    }
    case 0xec: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xed: {
      auto event = cursor.command("Tremolo", SequenceSemantic::Modulation);
      const u8 rawDelay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 delay = mystic && rawDelay == 0 ? 1 : rawDelay;
      event.derived("effective_delay", delay, SemanticOperandRole::Duration);
      event.derived("peak_attenuation_steps", static_cast<u8>(depth >> 1), SemanticOperandRole::Modulation);
      return event.invoke<&Playback::configureTremolo>(delay, rate, depth);
    }
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
      return event.invoke<&Playback::pitchSlide>(delay, duration, target, sequence.initialTranspose);
    }
    case 0xf4:
    case 0xf5:
      return ignored(cursor, "Unknown Driver State", mystic ? 3 : 0);
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
    case 0xf9: {
      auto event = cursor.command("Vibrato Fade", SequenceSemantic::Modulation);
      const u8 rawLength = event.u8("length", SemanticOperandRole::Duration);
      const u8 length = mystic && rawLength == 0 ? 1 : rawLength;
      event.derived("effective_length", length, SemanticOperandRole::Duration);
      return event.invoke<&Playback::setVibratoFade>(length);
    }
    case 0xfa: {
      auto event = cursor.command("Release Rate", SequenceSemantic::State);
      return event.set<&TrackState::releaseRate>(event.u8("rate"));
    }
    case 0xfb: {
      if (gx) {
        return ignored(cursor, "Unknown Driver State", 0);
      }
      if (sequence.indexedNoteTableOffset == 0) {
        return cursor.unsupported("Indexed Note Table Is Missing").stop();
      }
      auto event = cursor.command("Indexed Note Jump", SequenceSemantic::Jump);
      const u8 index = event.u8("index");
      const Address destination{
          static_cast<u64>(sequence.indexedNoteTableOffset) + static_cast<u64>(index) * 4,
      };
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return event.jump(destination);
    }
    case 0xfc:
      return ignored(cursor, "Unknown Driver State", 0);
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
              .initialReverbSend = 0.0,
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
  SequenceDecodeSession sequence{
      reader,
      konamiArcadeSequenceDialect(),
      sequenceAsset,
      sequenceLayout.trackTable,
      sourceMap,
      kMaxTrackCommands,
      static_cast<u32>(layout.code.endOffset()),
  };

  for (const KonamiArcadeTrackLayout& track : sequenceLayout.tracks) {
    std::array<Address, 2> discoveredLoops;
    Address discoveredSubroutine;
    const auto decode = [&](u32 offset) {
      return decodeCommand(reader, offset, layout, sequenceLayout, discoveredLoops, discoveredSubroutine, diagnostics);
    };
    sequence.addLinearTrack(track.number, track.pointer, track.offset, decode, track.encodedAddress);
  }

  SequenceProgram program = sequence.finish();
  program.config.profile = static_cast<u32>(layout.version);
  return program;
}

}  // namespace vgmtrans::formats::konami_arcade

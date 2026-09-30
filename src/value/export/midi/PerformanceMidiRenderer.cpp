/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PerformanceMidiRenderer.h"

#include "value/base/LevelScale.h"
#include "value/export/PerformancePitchBendContext.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/MidiTrackPlanner.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace {

// Immediately before per-voice tuning RPNs (priority 8) and all other attack state.
constexpr int kVoiceTerminationPriority = 7;

[[nodiscard]] u8 data7(double value) {
  return static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 127));
}

[[nodiscard]] u16 data14(double value) {
  return static_cast<u16>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 16383));
}

[[nodiscard]] u8 denominatorPower(u8 denominator) {
  if (denominator == 0) {
    return 0;
  }
  constexpr double ln2 = 0.69314718055994530942;
  return static_cast<u8>(std::log(static_cast<double>(denominator)) / ln2);
}

[[nodiscard]] MidiEvent tempoEvent(u64 tick, u32 microsecondsPerQuarter) {
  return midi::meta(
      tick, 0x51,
      {static_cast<u8>((microsecondsPerQuarter >> 16) & 0xff), static_cast<u8>((microsecondsPerQuarter >> 8) & 0xff),
       static_cast<u8>(microsecondsPerQuarter & 0xff)});
}

[[nodiscard]] MidiEvent timeSignatureEvent(u64 tick, u8 numerator, u8 denominator, u8 clocksPerMetronomeClick) {
  return midi::meta(tick, 0x58, {numerator, denominatorPower(denominator), clocksPerMetronomeClick, 8});
}

[[nodiscard]] u8 midiKey(double key) {
  return data7(key);
}

[[nodiscard]] u8 midiVelocity(double linearVelocity) {
  return LevelScale::midi7FromLinear(linearVelocity);
}

struct MidiChannelAssignment {
  size_t port = 0;
  u8 channel = 0;
};

[[nodiscard]] MidiChannelAssignment midiChannelAssignment(size_t trackIndex, const MidiExportOptions& options) {
  constexpr size_t channelsPerPort = 16;
  constexpr size_t skippedDrumChannel = 9;
  if (options.skipChannel10) {
    constexpr size_t usableChannelsPerPort = channelsPerPort - 1;
    const size_t port = trackIndex / usableChannelsPerPort;
    const size_t slot = trackIndex % usableChannelsPerPort;
    return MidiChannelAssignment{
        .port = port,
        .channel = static_cast<u8>(slot < skippedDrumChannel ? slot : slot + 1),
    };
  }

  return MidiChannelAssignment{
      .port = trackIndex / channelsPerPort,
      .channel = static_cast<u8>(trackIndex % channelsPerPort),
  };
}

[[nodiscard]] u8 midiPortByte(size_t port) {
  return static_cast<u8>(std::min<size_t>(port, 255));
}

[[nodiscard]] u8 midiPan(double stereoPosition) {
  return data7(((std::clamp(stereoPosition, -1.0, 1.0) + 1.0) / 2.0) * 127.0);
}

struct LoweredStereoBalance {
  u8 pan = 64;
  double gain = 1.0;
};

[[nodiscard]] LoweredStereoBalance lowerStereoBalance(double sourceLeft, double sourceRight) {
  constexpr double piOverTwo = 1.57079632679489661923;
  // MIDI pan cannot encode polarity. Retain the magnitude of a phase-inverted
  // source channel instead of incorrectly treating it as silence.
  sourceLeft = std::abs(sourceLeft);
  sourceRight = std::abs(sourceRight);

  // MIDI pan has only one position value, while source engines may specify two
  // independent channel gains. Pick the closest equal-power MIDI position,
  // then retain the scalar needed to reproduce the source gain vector.
  u8 pan = 64;
  if (sourceLeft != sourceRight) {
    const double arcPosition = std::atan2(sourceRight, sourceLeft) / piOverTwo;
    pan = static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(arcPosition * 126.0)), 0, 126));
    if (pan != 0) {
      ++pan;
    }
  }

  double midiGain = 1.0;
  if (pan == 64) {
    midiGain = std::sqrt(2.0);
  } else if (pan > 1 && pan < 127) {
    const double angle = piOverTwo * ((pan - 1) / 126.0);
    midiGain = std::cos(angle) + std::sin(angle);
  }

  return LoweredStereoBalance{
      .pan = pan,
      .gain = (sourceLeft + sourceRight) / midiGain,
  };
}

[[nodiscard]] LoweredStereoBalance lowerPositionalPan(PanLaw law, double stereoPosition) {
  const double position = std::clamp(stereoPosition, -1.0, 1.0);
  switch (law) {
    case PanLaw::ConstantSum: {
      const double rightGain = (position + 1.0) / 2.0;
      return lowerStereoBalance(1.0 - rightGain, rightGain);
    }
    case PanLaw::EqualPower:
      return LoweredStereoBalance{
          .pan = midiPan(position),
          .gain = 1.0,
      };
    case PanLaw::Unspecified:
      throw std::logic_error("Cannot render positional pan without a declared pan law");
  }
  throw std::logic_error("Unknown positional pan law");
}

[[nodiscard]] u8 midiNormalized7(double amount) {
  return data7(std::clamp(amount, 0.0, 1.0) * 127.0);
}

[[nodiscard]] s16 midiPitchBend(double semitones, u16 rangeCents) {
  if (rangeCents == 0) {
    return 0;
  }

  const double normalized = (semitones * 100.0) / static_cast<double>(rangeCents);
  return static_cast<s16>(std::clamp<int>(static_cast<int>(std::lround(normalized * 8192.0)), -8192, 8191));
}

[[nodiscard]] MidiLevelResolution resolveLevelResolution(MidiLevelResolution requested,
                                                         ValueQuantization quantization = {}) {
  if (requested != MidiLevelResolution::Auto) {
    return requested;
  }
  if (quantization.levels > 128) {
    return MidiLevelResolution::FourteenBit;
  }
  return MidiLevelResolution::SevenBit;
}

[[nodiscard]] bool writeBankSelectLsb(const MidiExportOptions& options) {
  return options.bankSelectStyle == MidiBankSelectStyle::MsbAndLsb;
}

struct MidiLevelState {
  MidiLevelResolution resolution;
  u16 value;

  friend bool operator==(const MidiLevelState&, const MidiLevelState&) = default;
};

struct MidiInstrumentSelection {
  InstrumentAddress address;
  bool forceBankSelect = false;
  std::optional<u16> pitchBendRangeCents;
};

[[nodiscard]] MidiInstrumentSelection instrumentSelection(const ResolvedInstrument& selection,
                                                          bool forceBankSelect = false) {
  const Instrument* instrument = selection.instrument;
  return MidiInstrumentSelection{
      .address = selection.address,
      .forceBankSelect = forceBankSelect,
      .pitchBendRangeCents = instrument != nullptr ? instrument->pitchBendRangeCents : std::nullopt,
  };
}

struct SimulatedLfoState {
  double depth = 0.0;
  double frequencyHz = 0.0;
  std::optional<double> cyclesPerTick;
  LfoDelay delay;
  LfoDelay noteRestartDelay;
  u32 delayCounterTicks = 0;
  double delayCounterMilliseconds = 0.0;
  u64 cursorTick = 0;
  double phaseCycles = 0.0;
  // The current track's temporary events own waveform tables through the final LFO flush.
  const LfoShape* shape = nullptr;
  LfoPolarity polarity = LfoPolarity::Bipolar;
  std::optional<double> initialPhaseCycles;
  std::optional<double> noteRestartInitialPhaseCycles;
  std::optional<ModulationRange> pitchRangeSemitones;
  u32 steppedDepthAttackSteps = 0;
  u32 activeSteppedDepthAttackSteps = 0;
  u32 steppedDepthAttackStep = 0;
  double steppedDepthAttackPhaseCycles = 0.0;
  bool sampleImmediatelyOnNote = false;
  u32 directionReversalTicks = 0;
  u32 directionTick = 0;
  bool phaseReversed = false;
  bool restartsOnNote = true;
  bool phaseRunsAtZeroDepth = false;
  bool delayRunsWhileInactive = true;
  bool outputHeldUntilNextNote = false;
  u64 noiseIndex = 0;
  double noiseValue = 0.0;
  PanLaw panLaw = PanLaw::Unspecified;
  bool started = false;
  bool producedSample = false;

  [[nodiscard]] bool canSampleImmediately() const {
    return sampleImmediatelyOnNote && delay.ticks == 0 && delay.milliseconds.value_or(0.0) <= 0.0 &&
           cyclesPerTick.value_or(frequencyHz) > 0.0 && depth > 0.0;
  }
};

struct SimulatedPitchLfoState {
  SimulatedLfoState oscillator;
  double semitones = 0.0;
};

[[nodiscard]] u16 wholeSemitonePitchBendRangeCents(u16 cents) {
  constexpr u32 kMinimumRangeCents = 200;
  constexpr u32 kMaximumWholeSemitoneRangeCents = 12'700;
  const u32 clamped = std::clamp<u32>(cents, kMinimumRangeCents, kMaximumWholeSemitoneRangeCents);
  return static_cast<u16>(((clamped + 99) / 100) * 100);
}

// Pitch layers are persistent and additive. Source wheels retain their
// normalized values so a source range change can reinterpret them.
class PitchBendLayers {
 public:
  void apply(const PitchBendPerformanceEvent& bend) {
    const double normalized = std::clamp(bend.normalizedWheelPosition.value_or(0.0), -1.0, 1.0);
    if (bend.semitones == 0.0 && normalized == 0.0) {
      layers.erase(bend.layer.value);
      return;
    }
    layers.insert_or_assign(bend.layer.value, bend);
  }

  [[nodiscard]] double semitones(const PerformancePitchBendContext& context) const {
    double total = 0.0;
    for (const auto& entry : layers) {
      total += context.semitones(entry.second);
    }
    return total;
  }

 private:
  std::map<u32, PitchBendPerformanceEvent> layers;
};

using detail::MidiNoteBoundary;
using detail::MidiInstrumentEvent;
using detail::MidiTrackEvent;
using MidiTimeline = std::vector<const MidiTrackEvent*>;

[[nodiscard]] double tuningBendSemitones(double cents, MidiTuningRendering rendering) {
  switch (rendering) {
    case MidiTuningRendering::PitchBend:
      return cents / 100.0;
    case MidiTuningRendering::CoarseAndFineTune:
      return 0.0;
  }
  throw std::logic_error("Unknown MIDI tuning rendering");
}

[[nodiscard]] std::vector<MidiTrackEvent> globalReverbEvents(const PerformanceSequence& performance) {
  std::vector<MidiTrackEvent> reverb;
  for (const auto& track : performance.tracks) {
    for (const auto& event : track.events) {
      if (const auto* change = std::get_if<ReverbPerformanceEvent>(&event); change && change->voiceMask) {
        reverb.emplace_back(*change);
      }
    }
  }
  return reverb;
}

[[nodiscard]] MidiTimeline midiTimeline(const std::vector<MidiTrackEvent>& events,
                                        const std::vector<MidiTrackEvent>& globalReverb) {
  MidiTimeline timeline;
  timeline.reserve(events.size() + globalReverb.size());
  for (const auto& event : events) {
    const auto* reverb = std::get_if<ReverbPerformanceEvent>(&event);
    if (!reverb || !reverb->voiceMask) {
      timeline.push_back(&event);
    }
  }
  for (const auto& event : globalReverb) {
    timeline.push_back(&event);
  }
  std::ranges::stable_sort(timeline, {},
                           [](const MidiTrackEvent* event) { return performanceEventHeader(*event).order(); });
  return timeline;
}

// MIDI CC7/CC11 cannot encode gain above unity. Reserve the minimum uniform
// sequence-wide headroom needed by source pan laws so every track keeps its
// relative level and source expression remains unclipped.
[[nodiscard]] double panLevelHeadroom(const PerformanceSequence& performance) {
  double maximumGain = 1.0;
  const auto observe = [&](double gain) {
    if (std::isfinite(gain)) {
      maximumGain = std::max(maximumGain, std::max(0.0, gain));
    }
  };

  for (const auto& track : performance.tracks) {
    double sourcePanLinearGain = 1.0;
    for (const auto& event : track.events) {
      if (const auto* pan = std::get_if<PanPerformanceEvent>(&event)) {
        sourcePanLinearGain = pan->linearGain;
        observe(lowerPositionalPan(pan->law, pan->stereoPosition).gain * sourcePanLinearGain);
      } else if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event)) {
        const double left = std::abs(balance->leftGain);
        const double right = std::abs(balance->rightGain);
        sourcePanLinearGain = left + right;
        observe(lowerStereoBalance(left, right).gain);
      } else if (std::holds_alternative<ChannelPanPerformanceEvent>(event)) {
        sourcePanLinearGain = 1.0;
      } else if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                 modulation != nullptr && (modulation->target == ModulationPerformanceTarget::PanDepth ||
                                           modulation->target == ModulationPerformanceTarget::PanRate)) {
        observe(sourcePanLinearGain);
      }
    }
  }
  return 1.0 / maximumGain;
}

// Pitch is resolved once, in musical units. Attacks delimit the intervals in
// which MIDI sensitivity must stay fixed; ties do not start a new interval.
struct PitchSample {
  u64 tick;
  double semitones;
  u16 minimumRangeCents;  // Source and instrument sensitivity remain lower bounds.
  bool attack = false;
};

void encodePitch(std::span<const PitchSample> samples, MidiTrack& track, u8 channel) {
  std::optional<u16> previousRange;
  s16 previousBend = 0;
  for (auto begin = samples.begin(); begin != samples.end();) {
    const auto end = std::find_if(std::next(begin), samples.end(), [](const auto& sample) { return sample.attack; });
    double cents = 200.0;
    for (auto sample = begin; sample != end; ++sample) {
      cents = std::max({cents, static_cast<double>(sample->minimumRangeCents), std::abs(sample->semitones) * 100.0});
    }
    const u16 range =
        wholeSemitonePitchBendRangeCents(static_cast<u16>(std::clamp(std::ceil(cents), 0.0, 12'700.0)));
    if (previousRange != range) {
      midi::appendRpn(track, begin->tick, channel, 0, 0, static_cast<u16>((range / 100) << 7));
      previousRange = range;
    }
    for (auto sample = begin; sample != end; ++sample) {
      const s16 bend = midiPitchBend(sample->semitones, range);
      if (previousBend != bend) {
        track.events.push_back(midi::pitchBend(sample->tick, channel, bend));
        previousBend = bend;
      }
    }
    begin = end;
  }
}

[[nodiscard]] s32 globalTransposeAt(std::span<const GlobalTransposePerformanceEvent* const> changes, u64 tick) {
  const auto upper =
      std::ranges::upper_bound(changes, tick, {}, [](const auto* change) { return change->header.tick; });
  return upper == changes.begin() ? 0 : (*std::prev(upper))->semitones;
}

[[nodiscard]] double lfoValue(LfoWaveform waveform, double phaseCycles) {
  const double phase = phaseCycles - std::floor(phaseCycles);
  switch (waveform) {
    case LfoWaveform::Sine:
      return std::sin(phase * 6.28318530717958647692);
    case LfoWaveform::Triangle:
      if (phase < 0.25) {
        return phase * 4.0;
      }
      if (phase < 0.75) {
        return 2.0 - (phase * 4.0);
      }
      return (phase * 4.0) - 4.0;
    case LfoWaveform::Square:
      return phase < 0.5 ? 1.0 : -1.0;
    case LfoWaveform::SawtoothUp:
      return (phase * 2.0) - 1.0;
    case LfoWaveform::SawtoothDown:
      return 1.0 - (phase * 2.0);
    case LfoWaveform::Noise:
      // Noise has no stable curve for the MIDI renderer to reproduce.
      return 0.0;
  }
  return 0.0;
}

[[nodiscard]] bool generatedNoise(const SimulatedLfoState& lfo) {
  return lfo.shape && lfo.shape->samples.empty() && lfo.shape->waveform == LfoWaveform::Noise;
}

void advanceNoise(SimulatedLfoState& lfo, u64 cycles) {
  lfo.noiseIndex += cycles;
  u32 value = static_cast<u32>(lfo.noiseIndex) + 0x6d2b79f5u;
  value = (value ^ (value >> 16)) * 0x7feb352du;
  value = (value ^ (value >> 15)) * 0x846ca68bu;
  value ^= value >> 16;
  lfo.noiseValue = static_cast<s8>(value & 0xff) / 128.0;
}

[[nodiscard]] double lfoValue(const SimulatedLfoState& lfo) {
  if (lfo.shape && !lfo.shape->samples.empty()) {
    const double phase = lfo.phaseCycles - std::floor(lfo.phaseCycles);
    const size_t index =
        std::min(lfo.shape->samples.size() - 1, static_cast<size_t>(std::floor(phase * lfo.shape->samples.size())));
    return std::clamp(lfo.shape->samples[index], -1.0, 1.0);
  }
  const double value = generatedNoise(lfo)
                           ? lfo.noiseValue
                           : lfoValue(lfo.shape ? lfo.shape->waveform : LfoWaveform::Triangle, lfo.phaseCycles);
  switch (lfo.polarity) {
    case LfoPolarity::Positive:
      return (value + 1.0) / 2.0;
    case LfoPolarity::Negative:
      return (value - 1.0) / 2.0;
    case LfoPolarity::Bipolar:
    default:
      return value;
  }
}

// Normalized tremolo events do not carry physical phase metadata. Preserve
// their established unipolar triangle start at nominal gain; physical events
// can provide initialPhaseCycles and bypass this renderer fallback entirely.
enum class LfoInitialPhaseFallback {
  Zero,
  UnipolarTremoloNominalGain,
};

[[nodiscard]] double initialLfoPhase(const SimulatedLfoState& lfo, LfoInitialPhaseFallback fallback) {
  if (lfo.initialPhaseCycles) {
    return *lfo.initialPhaseCycles - std::floor(*lfo.initialPhaseCycles);
  }
  return fallback == LfoInitialPhaseFallback::UnipolarTremoloNominalGain ? 0.75 : 0.0;
}

void applyLfoRestart(SimulatedLfoState& lfo, u64 tick, LfoRestartMode mode,
                     LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  if (mode == LfoRestartMode::None) {
    return;
  }
  if (mode == LfoRestartMode::PhaseAndDelay || mode == LfoRestartMode::Delay) {
    lfo.delayCounterTicks = 0;
    lfo.delayCounterMilliseconds = 0.0;
  }
  lfo.cursorTick = tick;
  if (mode == LfoRestartMode::Delay) {
    return;
  }
  lfo.phaseCycles = initialLfoPhase(lfo, fallback);
  lfo.activeSteppedDepthAttackSteps = lfo.steppedDepthAttackSteps;
  lfo.steppedDepthAttackStep = lfo.activeSteppedDepthAttackSteps == 0 ? 0 : 1;
  lfo.steppedDepthAttackPhaseCycles = 0.0;
  lfo.directionTick = 0;
  lfo.phaseReversed = false;
  if (generatedNoise(lfo)) {
    lfo.noiseValue = 0.0;
  }
  lfo.started = true;
  lfo.producedSample = false;
}

void applyLfoDelayUpdate(SimulatedLfoState& lfo, LfoDelay delay) {
  if (delay.milliseconds) {
    delay.milliseconds = std::max(0.0, *delay.milliseconds);
  }
  lfo.noteRestartDelay = delay;
  if (delay.updateMode == LfoDelayUpdateMode::CurrentAndFutureNotes) {
    lfo.delay = std::move(delay);
  }
}

void configureLfo(SimulatedLfoState& lfo, u64 tick, const ModulationPerformanceEvent& event,
                  LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  const LfoPerformanceContext& context = event.context;
  if (context.cyclesPerTick) {
    lfo.cyclesPerTick = std::max(0.0, *context.cyclesPerTick);
  } else if (context.frequencyHz) {
    lfo.cyclesPerTick.reset();
  }
  if (context.frequencyHz) {
    lfo.frequencyHz = std::max(0.0, *context.frequencyHz);
  }
  if (context.shape) {
    lfo.shape = &*context.shape;
  }
  if (context.polarity) {
    lfo.polarity = *context.polarity;
  }
  if (context.initialPhaseCycles) {
    lfo.initialPhaseCycles = context.initialPhaseCycles;
  }
  if (context.noteRestartInitialPhaseCycles) {
    lfo.noteRestartInitialPhaseCycles = context.noteRestartInitialPhaseCycles;
  }
  if (context.pitchRangeSemitones) {
    lfo.pitchRangeSemitones = context.pitchRangeSemitones;
  }
  if (context.steppedDepthAttackSteps) {
    lfo.steppedDepthAttackSteps = *context.steppedDepthAttackSteps;
  }
  lfo.sampleImmediatelyOnNote = context.sampleImmediatelyOnNote;
  if (context.directionReversalTicks) {
    lfo.directionReversalTicks = *context.directionReversalTicks;
  }
  if (context.panLaw != PanLaw::Unspecified) {
    lfo.panLaw = context.panLaw;
  }
  if (context.delay) {
    applyLfoDelayUpdate(lfo, *context.delay);
  }
  lfo.phaseRunsAtZeroDepth = context.phaseRunsAtZeroDepth;
  lfo.delayRunsWhileInactive = context.delayRunsWhileInactive;
  lfo.restartsOnNote = context.restartsOnNote;
  applyLfoRestart(lfo, tick, lfo.started ? context.restartMode : LfoRestartMode::PhaseAndDelay, fallback);
}

void restartNoteLfo(SimulatedLfoState& lfo, u64 tick,
                    LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  lfo.delay = lfo.noteRestartDelay;
  applyLfoRestart(lfo, tick, LfoRestartMode::PhaseAndDelay, fallback);
  if (lfo.noteRestartInitialPhaseCycles) {
    lfo.phaseCycles = *lfo.noteRestartInitialPhaseCycles - std::floor(*lfo.noteRestartInitialPhaseCycles);
  }
}

void setLfoDelay(SimulatedLfoState& lfo, u64 tick, LfoDelay delay,
                 LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  applyLfoDelayUpdate(lfo, delay);
  if (!lfo.started) {
    applyLfoRestart(lfo, tick, LfoRestartMode::PhaseAndDelay, fallback);
  }
}

template <class Apply>
void flushLfo(SimulatedLfoState& lfo, u64 upToTick, const PerformanceTempoMap& tempos, Apply&& apply) {
  if (!lfo.started || lfo.cursorTick >= upToTick) {
    return;
  }

  while (lfo.cursorTick < upToTick) {
    ++lfo.cursorTick;
    const u64 intervalTick = lfo.cursorTick == 0 ? 0 : lfo.cursorTick - 1;
    const double tickSeconds = tempos.tickSeconds(intervalTick);
    const double tickMilliseconds = tickSeconds * 1000.0;
    const bool inactive =
        lfo.cyclesPerTick.value_or(lfo.frequencyHz) <= 0.0 || (lfo.depth <= 0.0 && !lfo.phaseRunsAtZeroDepth);
    if (inactive && !lfo.delayRunsWhileInactive) {
      continue;
    }
    if (!lfo.delay.tempoRelative && lfo.delay.milliseconds) {
      if (lfo.delayCounterMilliseconds < *lfo.delay.milliseconds) {
        lfo.delayCounterMilliseconds =
            std::min(*lfo.delay.milliseconds, lfo.delayCounterMilliseconds + tickMilliseconds);
        if (lfo.delayCounterMilliseconds < *lfo.delay.milliseconds) {
          continue;
        }
      }
    } else if (lfo.delayCounterTicks < lfo.delay.ticks) {
      ++lfo.delayCounterTicks;
      if (lfo.delayCounterTicks < lfo.delay.ticks) {
        continue;
      }
    }

    if (inactive) {
      continue;
    }

    const double phaseStep = lfo.cyclesPerTick.value_or(lfo.frequencyHz * tickSeconds);
    const auto advancePhase = [&]() {
      const double phaseDelta = lfo.phaseReversed ? -phaseStep : phaseStep;
      const double unwrappedPhase = lfo.phaseCycles + phaseDelta;
      const double completedCycles = phaseDelta >= 0.0 ? std::floor(unwrappedPhase) : std::ceil(-unwrappedPhase);
      lfo.phaseCycles = std::fmod(unwrappedPhase, 1.0);
      if (lfo.phaseCycles < 0.0) {
        lfo.phaseCycles += 1.0;
      }
      if (generatedNoise(lfo) && completedCycles > 0.0) {
        advanceNoise(lfo, static_cast<u64>(std::min(completedCycles, 1000000.0)));
      }
      if (lfo.directionReversalTicks != 0 && ++lfo.directionTick == lfo.directionReversalTicks) {
        lfo.directionTick = 0;
        lfo.phaseReversed = !lfo.phaseReversed;
      }
      if (lfo.activeSteppedDepthAttackSteps != 0 && lfo.steppedDepthAttackStep < lfo.activeSteppedDepthAttackSteps) {
        const double attackPhase = lfo.steppedDepthAttackPhaseCycles + phaseStep;
        const u32 completedAttackCycles = static_cast<u32>(std::floor(attackPhase));
        lfo.steppedDepthAttackPhaseCycles = attackPhase - std::floor(attackPhase);
        lfo.steppedDepthAttackStep =
            std::min(lfo.activeSteppedDepthAttackSteps, lfo.steppedDepthAttackStep + completedAttackCycles);
      }
    };
    if (lfo.producedSample && lfo.sampleImmediatelyOnNote) {
      advancePhase();
    }
    const double value = lfoValue(lfo);
    if (lfo.depth > 0.0) {
      apply(lfo.cursorTick, value);
    }
    lfo.producedSample = true;
    if (!lfo.sampleImmediatelyOnNote) {
      advancePhase();
    }
  }
}

[[nodiscard]] double lfoDepthScale(const SimulatedLfoState& lfo) {
  if (lfo.activeSteppedDepthAttackSteps == 0) {
    return 1.0;
  }
  return static_cast<double>(lfo.steppedDepthAttackStep) / static_cast<double>(lfo.activeSteppedDepthAttackSteps);
}

[[nodiscard]] double simulatedVibratoAtPhase(const SimulatedLfoState& lfo, double value) {
  double semitones = lfo.depth * value;
  if (lfo.pitchRangeSemitones) {
    semitones = value >= 0.0 ? value * lfo.pitchRangeSemitones->maximum : -value * lfo.pitchRangeSemitones->minimum;
  }
  return semitones * lfoDepthScale(lfo);
}

[[nodiscard]] bool releaseHeldLfoOutput(SimulatedLfoState& lfo) {
  const bool wasHeld = lfo.outputHeldUntilNextNote;
  lfo.outputHeldUntilNextNote = false;
  return wasHeld;
}

// Simulate channel state once. Pitch stays in semitones until the complete
// path is available for sensitivity selection and MIDI encoding.
class MidiTrackRenderer {
public:
  MidiTrackRenderer(MidiTrack& track, u8 channel, const PerformanceTempoMap& tempos,
                    const MidiExportOptions& options, double headroom)
      : track(track), channel(channel), options(options), tempos(tempos), levelHeadroom(headroom) {}

  void render(const PerformanceTrack& source, const MidiTimeline& timeline,
              std::span<const GlobalTransposePerformanceEvent* const> globalTransposes,
              ModulationConversionPolicy modulationConversion, const ResolvedPerformance& resolved,
              const SequenceModulationProfile* modulationProfile) {
    pitchBendContext = PerformancePitchBendContext{resolved};
    recordPitch(0);
    for (auto event = timeline.begin(); event != timeline.end();) {
      const u64 tick = performanceEventHeader(**event).tick;
      // The periodic sample precedes this tick's commands. Commands retain
      // source order, and a reset replaces the sample at the same tick.
      flushModulation(tick, modulationConversion);
      bool attack = false;
      do {
        if (const auto* note = std::get_if<MidiNoteBoundary>(*event)) {
          attack |= note->attack.has_value();
        }
        addMidiEvent(**event, source.sourceTrackNumber, globalTransposes, modulationConversion, modulationProfile);
        ++event;
      } while (event != timeline.end() && performanceEventHeader(**event).tick == tick);
      recordPitch(tick, attack);
    }
    // Consuming a trailing source command must not shorten the modulation tail.
    const u64 endTick = source.events.empty() ? source.endTick
        : std::max(source.endTick, performanceEventHeader(source.events.back()).tick);
    flushModulation(endTick, modulationConversion);
    if (pitchRequested) {
      encodePitch(pitchSamples, track, channel);
    }
  }

private:
  MidiTrack& track;
  u8 channel;
  const MidiExportOptions& options;
  const PerformanceTempoMap& tempos;
  bool hasNote = false;
  // MIDI starts in bank/program zero.
  u16 midiBank = 0;
  u8 midiProgram = 0;
  PerformancePitchBendContext pitchBendContext;
  double tuningSemitones = 0.0;
  PitchBendLayers pitchBendLayers;
  std::map<u32, SimulatedPitchLfoState> pitchLfos;
  std::vector<PitchSample> pitchSamples;
  bool pitchRequested = false;
  double sourceLevelGain = 1.0;
  ValueQuantization sourceLevelQuantization;
  double panLevelGain = 1.0;
  double levelHeadroom = 1.0;
  // The channel has one value per controller, shared by all automations and
  // ordinary writes. Source commands can force a repeated write to survive.
  std::optional<MidiLevelState> lastVolume;
  std::optional<MidiLevelState> lastExpression;
  double sourceExpressionGain = 1.0;
  ValueQuantization sourceExpressionQuantization;
  double simulatedTremoloGain = 1.0;
  enum class TremoloDepthUnit {
    LegacyUnipolar,
    Decibels,
    LinearGain,
  };
  TremoloDepthUnit tremoloDepthUnit = TremoloDepthUnit::LegacyUnipolar;
  TremoloGainMode tremoloGainMode = TremoloGainMode::BipolarAroundNominal;
  SimulatedLfoState tremolo;
  double sourcePanPosition = 0.0;
  double sourcePanLinearGain = 1.0;
  PanLaw sourcePanLaw = PanLaw::Unspecified;
  double simulatedPanOffset = 0.0;
  std::optional<u8> lastPanValue;
  std::optional<u8> lastReverbValue;
  SimulatedLfoState panLfo;

  void addController(u64 tick, MidiController controller, s32 value, int priority = 20,
                     std::optional<double> normalizedAmount = std::nullopt) {
    track.events.push_back(midi::controller(tick, channel, controller, value, priority, normalizedAmount));
  }

  void addFineTune(u64 tick, double cents) {
    const double semitones = std::clamp(cents / 100.0, -1.0, 1.0);
    const s32 value = std::min(static_cast<int>(std::lround(8192 * semitones)), 8191) + 8192;
    midi::appendRpn(track, tick, channel, 0, 1, static_cast<u16>(value), 8);
  }

  void addCoarseTune(u64 tick, s8 semitones) {
    const s32 value = std::clamp<s32>((semitones + 64) << 7, 0, 16383);
    midi::appendRpn(track, tick, channel, 0, 2, static_cast<u16>(value), 8);
  }

  void addLevelController(std::optional<MidiLevelState>& state, u64 tick, MidiController controller, double linearGain,
                          MidiLevelResolution requestedResolution, ValueQuantization quantization, bool force) {
    const MidiLevelResolution resolution = resolveLevelResolution(requestedResolution, quantization);
    const u16 value = resolution == MidiLevelResolution::FourteenBit ? LevelScale::midi14FromLinear(linearGain)
                                                                     : LevelScale::midi7FromLinear(linearGain);
    const MidiLevelState nextState{.resolution = resolution, .value = value};
    if (!force && state == nextState) {
      return;
    }

    if (resolution == MidiLevelResolution::FourteenBit) {
      midi::appendController14(track, tick, channel, controller, value);
    } else {
      addController(tick, controller, static_cast<u8>(value));
    }
    state = nextState;
  }

  void addPan(std::optional<u8>& state, u64 tick, u8 value, bool force) {
    if (!force && state == value) {
      return;
    }
    addController(tick, MidiController::Pan, value);
    state = value;
  }

  [[nodiscard]] double layeredPitchBendSemitones() const { return pitchBendLayers.semitones(pitchBendContext); }

  [[nodiscard]] SimulatedPitchLfoState& pitchLfo(PitchBendLayerId layer) { return pitchLfos[layer.value]; }

  [[nodiscard]] double simulatedPitchLfoSemitones() const {
    double semitones = 0.0;
    for (const auto& entry : pitchLfos) {
      semitones += entry.second.semitones;
    }
    return semitones;
  }

  void recordPitch(u64 tick, bool attack = false) {
    const double semitones = tuningSemitones + layeredPitchBendSemitones() + simulatedPitchLfoSemitones();
    const u16 minimumRange = pitchBendContext.availableRangeCents();
    pitchRequested |= semitones != 0.0 || minimumRange > 200;
    // A tick has one final pitch, including any note reset or later source write.
    if (!pitchSamples.empty() && pitchSamples.back().tick == tick) {
      attack |= pitchSamples.back().attack;
      pitchSamples.pop_back();
    }
    if (attack || pitchSamples.empty() || pitchSamples.back().semitones != semitones ||
        pitchSamples.back().minimumRangeCents != minimumRange) {
      pitchSamples.push_back({tick, semitones, minimumRange, attack});
    }
  }

  void applyInstrumentSelection(u64 tick, const MidiInstrumentSelection& selection, bool forceProgramChange) {
    const u16 bank = static_cast<u16>(selection.address.bank & 0x3fff);
    const u16 emittedBank =
        options.bankSelectStyle == MidiBankSelectStyle::MsbOnly ? static_cast<u16>(bank & 0x7f) : bank;
    const bool bankChanged = emittedBank != midiBank;
    if (bankChanged || selection.forceBankSelect) {
      track.events.push_back(midi::bankSelect(tick, channel, bank, writeBankSelectLsb(options)));
      midiBank = emittedBank;
    }
    const u8 program = data7(selection.address.program);
    if (forceProgramChange || bankChanged || program != midiProgram) {
      midiProgram = program;
      track.events.push_back(midi::programChange(tick, channel, program));
    }
    pitchBendContext.setInstrumentRangeCents(selection.pitchBendRangeCents);
  }

  void flushModulation(u64 tick, ModulationConversionPolicy conversion) {
    flushSimulatedVibrato(tick);
    if (conversion == ModulationConversionPolicy::SequenceEventSimulation) {
      flushSimulatedTremolo(tick, conversion);
    }
    flushSimulatedPan(tick);
  }

  void flushSimulatedVibrato(u64 upToTick) {
    while (true) {
      std::optional<u64> nextTick;
      for (const auto& entry : pitchLfos) {
        const auto& pitch = entry.second;
        if (pitch.oscillator.cursorTick < upToTick) {
          nextTick = std::min(nextTick.value_or(pitch.oscillator.cursorTick + 1), pitch.oscillator.cursorTick + 1);
        }
      }
      if (!nextTick) {
        break;
      }
      bool sampled = false;
      for (auto& entry : pitchLfos) {
        auto& pitch = entry.second;
        flushLfo(pitch.oscillator, std::min(*nextTick, upToTick), tempos, [&](u64, double value) {
          pitch.semitones = simulatedVibratoAtPhase(pitch.oscillator, value);
          sampled = true;
        });
      }
      if (sampled) {
        recordPitch(*nextTick);
      }
    }
  }

  void setSimulatedVibratoDepth(double semitones, LfoZeroDepthBehavior zeroDepthBehavior, PitchBendLayerId layer) {
    auto& pitch = pitchLfo(layer);
    auto& lfo = pitch.oscillator;
    lfo.depth = std::max(0.0, semitones);
    lfo.outputHeldUntilNextNote =
        lfo.depth <= 0.0 && zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote;
    if (lfo.depth <= 0.0 && !lfo.outputHeldUntilNextNote) {
      pitch.semitones = 0.0;
    }
  }

  void updateRestartedVibratoOutput(const ModulationPerformanceEvent& event) {
    auto& pitch = pitchLfo(event.pitchLayer);
    auto& lfo = pitch.oscillator;
    if (event.context.restartMode != LfoRestartMode::PhaseAndDelay || lfo.outputHeldUntilNextNote) {
      return;
    }
    // A rate/depth event can restart vibrato on a held voice without a new MIDI
    // attack. Clear its previous sample during the new delay, just as a note
    // restart does, so later pitch slides do not inherit a frozen LFO offset.
    const bool startsImmediately = lfo.canSampleImmediately();
    const double value = startsImmediately ? simulatedVibratoAtPhase(lfo, lfoValue(lfo)) : 0.0;
    lfo.producedSample = startsImmediately;
    pitch.semitones = value;
  }

  void restartSimulatedVibratoForNote(u64 tick) {
    for (auto& entry : pitchLfos) {
      auto& pitch = entry.second;
      auto& lfo = pitch.oscillator;
      if (!lfo.started || !lfo.restartsOnNote) {
        continue;
      }
      restartNoteLfo(lfo, tick);
      lfo.outputHeldUntilNextNote = false;
      const bool startsImmediately = lfo.canSampleImmediately();
      pitch.semitones = startsImmediately ? simulatedVibratoAtPhase(lfo, lfoValue(lfo)) : 0.0;
      lfo.producedSample = startsImmediately;
    }
  }

  bool shouldRestartSimulatedVibratoForNote(const MidiNoteBoundary& note) const {
    if (!note.restartVibrato) {
      return false;
    }
    return std::ranges::any_of(pitchLfos, [](const auto& entry) {
      return entry.second.oscillator.started && entry.second.oscillator.restartsOnNote;
    });
  }

  void releaseHeldPitchLfoOutputs() {
    for (auto& entry : pitchLfos) {
      auto& pitch = entry.second;
      if (releaseHeldLfoOutput(pitch.oscillator) && pitch.semitones != 0.0) {
        pitch.semitones = 0.0;
      }
    }
  }

  // Pan-law conversion can require gain above unity. A sequence-wide headroom
  // factor bounds that gain without changing the mix between tracks. Keep it on
  // channel volume so source expression remains an independent control flow.
  void addCombinedLevel(u64 tick, bool force = true) {
    addLevelController(lastVolume, tick, MidiController::ChannelVolume, sourceLevelGain * panLevelGain * levelHeadroom,
                       options.volumeResolution, sourceLevelQuantization, force);
  }

  // Source expression and simulated tremolo share the remaining MIDI expression
  // controller. Pan conversion deliberately does not participate in this product.
  void addCombinedExpression(u64 tick, ModulationConversionPolicy modulationConversion, bool force = true) {
    const bool simulatingTremolo = modulationConversion == ModulationConversionPolicy::SequenceEventSimulation;
    addLevelController(lastExpression, tick, MidiController::Expression, sourceExpressionGain * simulatedTremoloGain,
                       options.expressionResolution,
                       simulatingTremolo ? ValueQuantization{} : sourceExpressionQuantization, force);
  }

  [[nodiscard]] double tremoloGain(double lfoValue) const {
    const double depth = tremolo.depth * lfoDepthScale(tremolo);
    if (tremoloDepthUnit == TremoloDepthUnit::Decibels) {
      double gainDecibels = depth * lfoValue;
      if (tremoloGainMode == TremoloGainMode::NoBoost) {
        gainDecibels -= depth;
      }
      return std::pow(10.0, gainDecibels / 20.0);
    }
    if (tremoloDepthUnit == TremoloDepthUnit::LinearGain) {
      return std::max(0.0, 1.0 + depth * lfoValue);
    }

    const double normalizedLfo = (lfoValue + 1.0) / 2.0;
    return 1.0 - (depth * normalizedLfo);
  }

  void flushSimulatedTremolo(u64 upToTick, ModulationConversionPolicy modulationConversion) {
    flushLfo(tremolo, upToTick, tempos, [&](u64 tick, double value) {
      simulatedTremoloGain = tremoloGain(value);
      addCombinedExpression(tick, modulationConversion);
    });
  }

  [[nodiscard]] double tremoloGainAtCurrentPhase() const {
    if (tremolo.depth <= 0.0) {
      return 1.0;
    }

    const double value = lfoValue(tremolo);
    return tremoloGain(value);
  }

  void setSimulatedTremoloDepth(u64 tick, double depth, TremoloDepthUnit unit, TremoloGainMode gainMode,
                                LfoZeroDepthBehavior zeroDepthBehavior,
                                ModulationConversionPolicy modulationConversion) {
    tremolo.depth = std::max(0.0, depth);
    tremoloDepthUnit = unit;
    tremoloGainMode = gainMode;
    tremolo.outputHeldUntilNextNote =
        tremolo.depth <= 0.0 && zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote;
    if (tremolo.outputHeldUntilNextNote) {
      return;
    }
    const double gain = tremoloGainAtCurrentPhase();
    if (gain != simulatedTremoloGain) {
      simulatedTremoloGain = gain;
      addCombinedExpression(tick, modulationConversion);
    }
  }

  void restartSimulatedTremoloForNote(u64 tick, ModulationConversionPolicy modulationConversion) {
    if (!tremolo.started) {
      return;
    }

    const auto fallback = !tremolo.shape && tremoloDepthUnit == TremoloDepthUnit::LegacyUnipolar
                              ? LfoInitialPhaseFallback::UnipolarTremoloNominalGain
                              : LfoInitialPhaseFallback::Zero;
    restartNoteLfo(tremolo, tick, fallback);
    tremolo.outputHeldUntilNextNote = false;
    const bool delayed = tremolo.delay.ticks != 0 || tremolo.delay.milliseconds.value_or(0.0) > 0.0;
    const double gain = delayed ? 1.0 : tremoloGainAtCurrentPhase();
    tremolo.producedSample = tremolo.canSampleImmediately();
    if (gain != simulatedTremoloGain) {
      simulatedTremoloGain = gain;
      addCombinedExpression(tick, modulationConversion);
    }
  }

  bool shouldRestartSimulatedTremoloForNote(const MidiNoteBoundary& note) const {
    return note.restartTremolo && tremolo.started;
  }

  void addCombinedPan(u64 tick, bool force = false) {
    const double position = std::clamp(sourcePanPosition + simulatedPanOffset, -1.0, 1.0);
    const PanLaw law = panLfo.panLaw != PanLaw::Unspecified ? panLfo.panLaw : sourcePanLaw;
    u8 value = midiPan(position);
    double levelGain = panLevelGain;
    if (law != PanLaw::Unspecified) {
      const LoweredStereoBalance lowered = lowerPositionalPan(law, position);
      value = lowered.pan;
      levelGain = lowered.gain * sourcePanLinearGain;
    }
    addPan(lastPanValue, tick, value, force);
    if (levelGain != panLevelGain) {
      panLevelGain = levelGain;
      addCombinedLevel(tick, force);
    }
  }

  void flushSimulatedPan(u64 upToTick) {
    flushLfo(panLfo, upToTick, tempos, [&](u64 tick, double value) {
      simulatedPanOffset = panLfo.depth * value;
      addCombinedPan(tick);
    });
  }

  void setSimulatedPanDepth(u64 tick, double depth) {
    panLfo.depth = std::max(0.0, depth);
    if (panLfo.depth <= 0.0 && simulatedPanOffset != 0.0) {
      simulatedPanOffset = 0.0;
      addCombinedPan(tick);
    }
  }

  void restartSimulatedPanForNote(u64 tick) {
    if (!panLfo.started) {
      return;
    }
    restartNoteLfo(panLfo, tick);
    if (simulatedPanOffset != 0.0) {
      simulatedPanOffset = 0.0;
      addCombinedPan(tick);
    }
  }

  bool shouldRestartSimulatedPanForNote(const MidiNoteBoundary& note) const {
    return note.restartPan && panLfo.started;
  }

  void addMidiEvent(const MidiTrackEvent& event, u32 sourceTrackNumber,
                    std::span<const GlobalTransposePerformanceEvent* const> globalTransposes,
                    ModulationConversionPolicy modulationConversion, const SequenceModulationProfile* modulationProfile) {
    const bool forceControllers = !performanceEventHeader(event).automation;
    std::visit(
        [&](const auto& typedEvent) {
          using TypedEvent = std::decay_t<decltype(typedEvent)>;
          if constexpr (std::is_same_v<TypedEvent, MidiNoteBoundary>) {
            if (typedEvent.expired) {
              return;
            }
            if (typedEvent.instrument) {
              auto selection = instrumentSelection(*typedEvent.instrument);
              applyInstrumentSelection(typedEvent.header.tick, selection, false);
            }
            if (shouldRestartSimulatedVibratoForNote(typedEvent)) {
              restartSimulatedVibratoForNote(typedEvent.header.tick);
            } else {
              releaseHeldPitchLfoOutputs();
            }
            if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
              if (shouldRestartSimulatedTremoloForNote(typedEvent)) {
                restartSimulatedTremoloForNote(typedEvent.header.tick, modulationConversion);
              } else if (releaseHeldLfoOutput(tremolo) && simulatedTremoloGain != 1.0) {
                simulatedTremoloGain = 1.0;
                addCombinedExpression(typedEvent.header.tick, modulationConversion);
              }
            }
            if (shouldRestartSimulatedPanForNote(typedEvent)) {
              restartSimulatedPanForNote(typedEvent.header.tick);
            }
            if (!typedEvent.attack) {
              return;
            }
            const auto& attack = *typedEvent.attack;
            if (attack.silencePreviousVoice && hasNote) {
              // Silence the old voice before bank, range, controller, or bend
              // state for this attack can affect it.
              addController(typedEvent.header.tick, MidiController::AllSoundOff, 0, kVoiceTerminationPriority);
            }
            if (!lastVolume && (levelHeadroom != 1.0 || sourceLevelGain != 1.0 || panLevelGain != 1.0)) {
              addCombinedLevel(typedEvent.header.tick);
            }
            hasNote = true;
            const u8 key = midiKey(attack.key + globalTransposeAt(globalTransposes, typedEvent.header.tick));
            track.events.push_back(
                midi::note(typedEvent.header.tick, channel, key, midiVelocity(attack.linearVelocity), attack.durationTicks));
          } else if constexpr (std::is_same_v<TypedEvent, MidiInstrumentEvent>) {
            const auto selection = instrumentSelection(typedEvent.selection, typedEvent.forceBankSelect);
            applyInstrumentSelection(typedEvent.header.tick, selection, true);
          } else if constexpr (std::is_same_v<TypedEvent, LevelPerformanceEvent>) {
            sourceLevelGain = typedEvent.linearGain;
            sourceLevelQuantization = typedEvent.sourceQuantization;
            addCombinedLevel(typedEvent.header.tick, forceControllers);
          } else if constexpr (std::is_same_v<TypedEvent, ExpressionPerformanceEvent>) {
            sourceExpressionGain = typedEvent.linearGain;
            sourceExpressionQuantization = typedEvent.sourceQuantization;
            addCombinedExpression(typedEvent.header.tick, modulationConversion, forceControllers);
          } else if constexpr (std::is_same_v<TypedEvent, PanPerformanceEvent>) {
            sourcePanPosition = typedEvent.stereoPosition;
            sourcePanLinearGain = typedEvent.linearGain;
            sourcePanLaw = typedEvent.law;
            addCombinedPan(typedEvent.header.tick, forceControllers);
          } else if constexpr (std::is_same_v<TypedEvent, ChannelPanPerformanceEvent>) {
            // MIDI/SF2 already applies CC10 to each voice's intrinsic region pan.
            // Preserve that native composition without aggregate-pan gain repair.
            sourcePanPosition = 0.0;
            sourcePanLinearGain = 1.0;
            sourcePanLaw = PanLaw::Unspecified;
            simulatedPanOffset = 0.0;
            panLfo = {};
            const u8 value = data7(std::clamp(typedEvent.position, 0.0, 1.0) * 127.0);
            addPan(lastPanValue, typedEvent.header.tick, value, forceControllers);
            if (panLevelGain != 1.0) {
              panLevelGain = 1.0;
              addCombinedLevel(typedEvent.header.tick, forceControllers);
            }
          } else if constexpr (std::is_same_v<TypedEvent, StereoBalancePerformanceEvent>) {
            const LoweredStereoBalance lowered = lowerStereoBalance(typedEvent.leftGain, typedEvent.rightGain);
            const double left = std::abs(typedEvent.leftGain);
            const double right = std::abs(typedEvent.rightGain);
            const double sum = left + right;
            sourcePanPosition = sum == 0.0 ? 0.0 : (right - left) / sum;
            sourcePanLinearGain = sum;
            sourcePanLaw = PanLaw::Unspecified;
            addPan(lastPanValue, typedEvent.header.tick, lowered.pan, forceControllers);
            panLevelGain = lowered.gain;
            addCombinedLevel(typedEvent.header.tick, forceControllers);
          } else if constexpr (std::is_same_v<TypedEvent, MasterLevelPerformanceEvent>) {
            const u16 value = LevelScale::midi14FromLinear(typedEvent.linearGain);
            track.events.push_back(midi::sysex(
                typedEvent.header.tick,
                {0x7f, 0x7f, 0x04, 0x01, static_cast<u8>(value & 0x7f), static_cast<u8>((value >> 7) & 0x7f), 0xf7}));
          } else if constexpr (std::is_same_v<TypedEvent, ReverbPerformanceEvent>) {
            const bool enabled = !typedEvent.voiceMask ||
                                 (sourceTrackNumber < 8 && (*typedEvent.voiceMask & (1u << sourceTrackNumber)) != 0);
            const u8 value = midiNormalized7(enabled ? typedEvent.send : 0.0);
            // Source DSP parameters can change without changing MIDI's single wet-send control.
            if (!lastReverbValue || *lastReverbValue != value) {
              addController(typedEvent.header.tick, MidiController::Reverb, value);
              lastReverbValue = value;
            }
          } else if constexpr (std::is_same_v<TypedEvent, MonoModePerformanceEvent>) {
            addController(typedEvent.header.tick, MidiController::MonoMode, typedEvent.channels);
          } else if constexpr (std::is_same_v<TypedEvent, TuningPerformanceEvent>) {
            if (options.tuning == MidiTuningRendering::CoarseAndFineTune) {
              const s32 coarse = std::clamp<s32>(static_cast<s32>(typedEvent.cents / 100.0), -64, 63);
              addCoarseTune(typedEvent.header.tick, static_cast<s8>(coarse));
              addFineTune(typedEvent.header.tick, typedEvent.cents - coarse * 100.0);
            }
            tuningSemitones = tuningBendSemitones(typedEvent.cents, options.tuning);
          } else if constexpr (std::is_same_v<TypedEvent, PitchBendPerformanceEvent>) {
            pitchBendLayers.apply(typedEvent);
            pitchRequested = true;
          } else if constexpr (std::is_same_v<TypedEvent, PitchBendRangePerformanceEvent>) {
            pitchBendContext.setSourceRangeCents(typedEvent.cents);
            pitchRequested = true;
          } else if constexpr (std::is_same_v<TypedEvent, PortamentoPerformanceEvent>) {
            if (typedEvent.timeMilliseconds) {
              midi::appendController14(track, typedEvent.header.tick, channel, MidiController::PortamentoTime,
                                       data14(*typedEvent.timeMilliseconds), true);
            }
            if (typedEvent.previousKey) {
              const double previousKey =
                  *typedEvent.previousKey + globalTransposeAt(globalTransposes, typedEvent.header.tick);
              addController(typedEvent.header.tick, MidiController::PortamentoControl, midiKey(previousKey));
            }
          } else if constexpr (std::is_same_v<TypedEvent, PortamentoEnablePerformanceEvent>) {
            addController(typedEvent.header.tick, MidiController::Portamento, typedEvent.enabled ? 127 : 0);
          } else if constexpr (std::is_same_v<TypedEvent, LegatoPedalPerformanceEvent>) {
            addController(typedEvent.header.tick, MidiController::Legato, typedEvent.enabled ? 127 : 0);
          } else if constexpr (std::is_same_v<TypedEvent, ModulationPerformanceEvent>) {
            const double normalizedAmount = modulationControllerAmount(typedEvent, modulationProfile);
            const u8 value = midiNormalized7(normalizedAmount);
            if (typedEvent.target == ModulationPerformanceTarget::VibratoDelay ||
                typedEvent.target == ModulationPerformanceTarget::TremoloDelay) {
              const bool vibrato = typedEvent.target == ModulationPerformanceTarget::VibratoDelay;
              if (typedEvent.context.delay) {
                const auto& delay = *typedEvent.context.delay;
                auto& lfo = vibrato ? pitchLfo(typedEvent.pitchLayer).oscillator : tremolo;
                const auto fallback = vibrato || delay.milliseconds
                                          ? LfoInitialPhaseFallback::Zero
                                          : LfoInitialPhaseFallback::UnipolarTremoloNominalGain;
                setLfoDelay(lfo, typedEvent.header.tick, delay, fallback);
              }
              if (modulationConversion != ModulationConversionPolicy::SequenceEventSimulation &&
                  (!vibrato || typedEvent.pitchLayer == kPrimaryPitchBendLayer)) {
                addController(typedEvent.header.tick,
                              vibrato ? MidiController::VibratoDelay : MidiController::TremoloDelay, value);
              }
              return;
            }
            const bool pitchTarget = typedEvent.target == ModulationPerformanceTarget::VibratoDepth ||
                                     typedEvent.target == ModulationPerformanceTarget::VibratoRate;
            if (pitchTarget && (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation ||
                                typedEvent.pitchLayer != kPrimaryPitchBendLayer)) {
              auto& lfo = pitchLfo(typedEvent.pitchLayer).oscillator;
              configureLfo(lfo, typedEvent.header.tick, typedEvent);
              if (typedEvent.target == ModulationPerformanceTarget::VibratoDepth) {
                setSimulatedVibratoDepth(
                    typedEvent.pitchDepthSemitones.value_or(std::clamp(typedEvent.amount, 0.0, 1.0) * 2.0),
                    typedEvent.context.zeroDepthBehavior, typedEvent.pitchLayer);
              }
              pitchRequested = true;
              updateRestartedVibratoOutput(typedEvent);
              return;
            }
            // MIDI has no pan-LFO controller, so both policies simulate it.
            if (typedEvent.target == ModulationPerformanceTarget::PanDepth ||
                typedEvent.target == ModulationPerformanceTarget::PanRate) {
              configureLfo(panLfo, typedEvent.header.tick, typedEvent);
              if (typedEvent.target == ModulationPerformanceTarget::PanDepth) {
                setSimulatedPanDepth(typedEvent.header.tick, typedEvent.panDepth.value_or(normalizedAmount));
              }
              return;
            }
            if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
              if (typedEvent.target == ModulationPerformanceTarget::TremoloDepth) {
                const bool physicalDecibels = typedEvent.volumeDepthDecibels.has_value();
                const bool physicalLinearGain = typedEvent.volumeDepthLinearGain.has_value();
                const auto fallback = !typedEvent.context.shape && !physicalDecibels && !physicalLinearGain
                                          ? LfoInitialPhaseFallback::UnipolarTremoloNominalGain
                                          : LfoInitialPhaseFallback::Zero;
                configureLfo(tremolo, typedEvent.header.tick, typedEvent, fallback);
                const auto unit = physicalDecibels ? TremoloDepthUnit::Decibels
                                                   : (physicalLinearGain ? TremoloDepthUnit::LinearGain
                                                                         : TremoloDepthUnit::LegacyUnipolar);
                setSimulatedTremoloDepth(typedEvent.header.tick,
                                         physicalDecibels
                                             ? *typedEvent.volumeDepthDecibels
                                             : (physicalLinearGain ? *typedEvent.volumeDepthLinearGain
                                                                   : std::clamp(typedEvent.amount, 0.0, 1.0) * 0.5),
                                         unit, typedEvent.context.tremoloGainMode, typedEvent.context.zeroDepthBehavior,
                                         modulationConversion);
              } else if (typedEvent.target == ModulationPerformanceTarget::TremoloRate) {
                configureLfo(tremolo, typedEvent.header.tick, typedEvent,
                             typedEvent.context.shape ? LfoInitialPhaseFallback::Zero
                                                      : LfoInitialPhaseFallback::UnipolarTremoloNominalGain);
              }
              return;
            }
            switch (typedEvent.target) {
              case ModulationPerformanceTarget::VibratoDepth:
                addController(typedEvent.header.tick, MidiController::Modulation, value, 20, normalizedAmount);
                break;
              case ModulationPerformanceTarget::VibratoRate:
                addController(typedEvent.header.tick, MidiController::VibratoRate, value, 20, normalizedAmount);
                break;
              case ModulationPerformanceTarget::TremoloDepth:
                addController(typedEvent.header.tick, MidiController::TremoloDepth, value, 20, normalizedAmount);
                break;
              case ModulationPerformanceTarget::TremoloRate:
                addController(typedEvent.header.tick, MidiController::TremoloRate, value, 20, normalizedAmount);
                break;
              case ModulationPerformanceTarget::PanDepth:
              case ModulationPerformanceTarget::PanRate:
              case ModulationPerformanceTarget::VibratoDelay:
              case ModulationPerformanceTarget::TremoloDelay:
                break;
            }
          } else if constexpr (std::is_same_v<TypedEvent, MarkerPerformanceEvent>) {
            track.events.push_back(midi::meta(typedEvent.header.tick, 0x06,
                                              std::vector<u8>(typedEvent.text.begin(), typedEvent.text.end()), 90));
          }
        },
        event);
  }
};

}  // namespace

MidiSequence renderMidiSequence(const ResolvedPerformance& resolved,
                                MidiExportOptions options, ModulationConversionPolicy modulationConversion,
                          const SequenceModulationProfile* modulationProfile) {
  const auto& performance = resolved.performance();
  if (!resolved.valid()) return MidiSequence{.diagnostics = performance.diagnostics};
  std::optional<SequenceModulationProfile> derivedModulationProfile;
  if (modulationProfile == nullptr) {
    derivedModulationProfile = analyzeSequenceModulation(performance);
    modulationProfile = &*derivedModulationProfile;
  }

  const PerformanceTempoMap globalTempos{performance};
  MidiSequence sequence{
      .timebase = performance.timebase,
      .diagnostics = performance.diagnostics,
  };
  sequence.tracks.reserve(performance.tracks.size());
  const auto globalReverb = globalReverbEvents(performance);
  const auto globalTransposes = orderedPerformanceEvents<GlobalTransposePerformanceEvent>(performance);
  const auto globalTimeSignatures = orderedPerformanceEvents<TimeSignaturePerformanceEvent>(performance);
  const double levelHeadroom = panLevelHeadroom(performance);

  for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
    const auto& performanceTrack = performance.tracks[trackIndex];
    const auto events = detail::planMidiTrack(resolved, trackIndex, options, globalTempos, sequence.diagnostics);
    const auto timeline = midiTimeline(events, globalReverb);
    MidiTrack midiTrack{
        .name = performanceTrack.name.empty() ? "Track " + std::to_string(performanceTrack.sourceTrackNumber)
                                              : performanceTrack.name,
    };
    const auto assignment = midiChannelAssignment(trackIndex, options);
    MidiTrackRenderer renderer{midiTrack, assignment.channel,
                               globalTempos, options, levelHeadroom};
    if (options.writePortMetaEvents) {
      midiTrack.events.push_back(midi::meta(0, 0x21, {midiPortByte(assignment.port)}, -5));
    }
    renderer.render(performanceTrack, timeline, globalTransposes, modulationConversion, resolved,
                    modulationProfile);
    u64 endTick = performanceTrack.endTick;
    if (trackIndex == 0) {
      for (const auto& tempo : globalTempos.points()) {
        midiTrack.events.push_back(tempoEvent(tempo.tick, tempo.microsecondsPerQuarter));
        endTick = std::max(endTick, tempo.tick);
      }
      for (const auto* timeSignature : globalTimeSignatures) {
        midiTrack.events.push_back(timeSignatureEvent(timeSignature->header.tick, timeSignature->numerator,
                                                      timeSignature->denominator,
                                                      timeSignature->clocksPerMetronomeClick));
        endTick = std::max(endTick, timeSignature->header.tick);
      }
    }
    midiTrack.endTick = endTick;
    sequence.tracks.push_back(std::move(midiTrack));
  }

  // Keep source/lowering diagnostics before channel-encoding warnings.
  for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
    if (midiChannelAssignment(trackIndex, options).port > 255) {
      sequence.diagnostics.push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "MIDI port number exceeded the Standard MIDI File port meta-event range",
      });
    }
  }
  return sequence;
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PerformanceMidiRenderer.h"

#include "value/base/LevelScale.h"
#include "value/export/PerformanceInstrumentSelection.h"
#include "value/export/PerformancePitchBendContext.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
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

[[nodiscard]] MidiInstrumentSelection instrumentSelection(const InstrumentSelection& selection,
                                                          std::span<const SoundBankAsset* const> soundBanks,
                                                          bool forceBankSelect = false) {
  const Instrument* instrument = findPerformanceInstrument(selection, soundBanks);
  return MidiInstrumentSelection{
      .address = instrument ? resolveInstrumentAddress(instrument->explicitAddress, instrument->identity)
                            : resolveInstrumentAddress(selection),
      .forceBankSelect = std::holds_alternative<InstrumentIdentity>(selection) || forceBankSelect,
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
  // The immutable lowered performance owns waveform tables throughout rendering.
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

// Pitch layers are persistent and additive. Keep their replacement semantics
// and source-wheel conversion in one place so range planning and rendering
// cannot disagree about the resulting pitch.
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

[[nodiscard]] u64 physicalNoteEnd(const NotePerformanceEvent& note, const PerformanceTempoMap& tempos) {
  const auto ticks = tempos.durationTicksForMilliseconds(note.header.tick, *note.maximumDurationMilliseconds);
  return addTicks(note.header.tick, ticks);
}

// Only these physical MIDI notes belong to this source voice.
struct RenderVoice {
  std::optional<u64> endLimit;
  std::vector<size_t> fragments;
};

using PerformanceTimeline = std::vector<const PerformanceEvent*>;
using PerformanceTimelines = std::vector<PerformanceTimeline>;

struct VoicePitchBendRangeChange {
  u64 tick = 0;
  u64 sequence = 0;
  u16 sourceCents = 200;
  std::optional<u16> voiceCents;
};

[[nodiscard]] double tuningBendSemitones(double cents, MidiTuningRendering rendering) {
  switch (rendering) {
    case MidiTuningRendering::PitchBend:
      return cents / 100.0;
    case MidiTuningRendering::CoarseAndFineTune:
      return 0.0;
  }
  throw std::logic_error("Unknown MIDI tuning rendering");
}

[[nodiscard]] PerformanceTimelines buildPerformanceTimelines(const PerformanceSequence& performance) {
  PerformanceTimelines timelines;
  PerformanceTimeline globalReverb;
  timelines.reserve(performance.tracks.size());
  for (const auto& track : performance.tracks) {
    auto& timeline = timelines.emplace_back();
    timeline.reserve(track.events.size());
    for (const auto& event : track.events) {
      const auto* reverb = std::get_if<ReverbPerformanceEvent>(&event);
      if (reverb != nullptr && reverb->voiceMask) {
        globalReverb.push_back(&event);
      } else {
        timeline.push_back(&event);
      }
    }
  }
  for (auto& timeline : timelines) {
    timeline.insert(timeline.end(), globalReverb.begin(), globalReverb.end());
    std::ranges::stable_sort(timeline, {},
                             [](const PerformanceEvent* event) { return performanceEventHeader(*event).order(); });
  }
  return timelines;
}

// MIDI CC7/CC11 cannot encode gain above unity. Reserve the minimum uniform
// sequence-wide headroom needed by source pan laws so every track keeps its
// relative level and source expression remains unclipped.
[[nodiscard]] double panLevelHeadroom(const PerformanceTimelines& timelines) {
  double maximumGain = 1.0;
  const auto observe = [&](double gain) {
    if (std::isfinite(gain)) {
      maximumGain = std::max(maximumGain, std::max(0.0, gain));
    }
  };

  for (const auto& timeline : timelines) {
    double sourcePanLinearGain = 1.0;
    for (const PerformanceEvent* event : timeline) {
      if (const auto* pan = std::get_if<PanPerformanceEvent>(event)) {
        sourcePanLinearGain = pan->linearGain;
        observe(lowerPositionalPan(pan->law, pan->stereoPosition).gain * sourcePanLinearGain);
      } else if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(event)) {
        const double left = std::abs(balance->leftGain);
        const double right = std::abs(balance->rightGain);
        sourcePanLinearGain = left + right;
        observe(lowerStereoBalance(left, right).gain);
      } else if (std::holds_alternative<ChannelPanPerformanceEvent>(*event)) {
        sourcePanLinearGain = 1.0;
      } else if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(event);
                 modulation != nullptr && (modulation->target == ModulationPerformanceTarget::PanDepth ||
                                           modulation->target == ModulationPerformanceTarget::PanRate)) {
        observe(sourcePanLinearGain);
      }
    }
  }
  return 1.0 / maximumGain;
}

// Pitch-bend sensitivity is channel state. Reserve one stable range from each
// physical attack through every linked note in that sounding voice.
[[nodiscard]] std::vector<VoicePitchBendRangeChange> planVoicePitchBendRanges(const PerformanceTimeline& timeline,
                                                                              MidiTuningRendering tuningRendering,
                                                                              std::span<const SoundBankAsset* const>
                                                                                  soundBanks) {
  struct Voice {
    u64 startTick = 0;
    u64 startSequence = 0;
    u16 sourceCents = 200;
    double bendExtent = 0.0;
    bool hasAutomatedBend = false;
    bool exceedsAvailableRange = false;
  };

  std::vector<Voice> voices;
  for (const auto* event : timeline) {
    const auto* note = std::get_if<NotePerformanceEvent>(event);
    if (note != nullptr && !note->extendsPrevious && (voices.empty() || voices.back().startTick != note->header.tick)) {
      voices.push_back(Voice{.startTick = note->header.tick, .startSequence = note->header.sequence});
    }
  }

  size_t nextVoice = 0;
  size_t activeVoice = voices.size();
  PerformancePitchBendContext pitchContext{soundBanks};
  PitchBendLayers activeBendLayers;
  double activeTuningBend = 0.0;
  const auto observePitch = [&] {
    if (activeVoice == voices.size()) {
      return;
    }
    auto& voice = voices[activeVoice];
    const double bend = activeTuningBend + activeBendLayers.semitones(pitchContext);
    voice.bendExtent = std::max(voice.bendExtent, std::abs(bend));
    const u16 availableCents = wholeSemitonePitchBendRangeCents(pitchContext.availableRangeCents());
    voice.exceedsAvailableRange |= std::abs(bend) * 100.0 > availableCents;
  };
  for (const auto* event : timeline) {
    const auto& header = performanceEventHeader(*event);
    bool pitchChanged = false;
    while (nextVoice < voices.size() &&
           std::pair{voices[nextVoice].startTick, voices[nextVoice].startSequence} <= header.order()) {
      activeVoice = nextVoice++;
      voices[activeVoice].sourceCents = pitchContext.sourceRangeCents();
      pitchChanged = true;
    }
    if (pitchContext.apply(*event, soundBanks)) {
      pitchChanged = true;
    } else if (const auto* tuning = std::get_if<TuningPerformanceEvent>(event)) {
      activeTuningBend = tuningBendSemitones(tuning->cents, tuningRendering);
      pitchChanged = true;
    } else if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(event)) {
      activeBendLayers.apply(*bend);
      if (activeVoice != voices.size()) {
        voices[activeVoice].hasAutomatedBend |= bend->header.automation.has_value();
      }
      pitchChanged = true;
    }
    if (pitchChanged) {
      observePitch();
    }
  }

  std::vector<VoicePitchBendRangeChange> changes;
  std::optional<u16> activeRange;
  for (const auto& voice : voices) {
    const u16 requiredCents = static_cast<u16>(
        std::clamp(std::ceil(voice.bendExtent * 100.0), 0.0, 12'700.0));
    const std::optional<u16> range = voice.hasAutomatedBend || voice.exceedsAvailableRange
                                         ? std::optional{std::max<u16>(200, requiredCents)}
                                         : std::nullopt;
    if (range != activeRange) {
      changes.push_back(VoicePitchBendRangeChange{
          .tick = voice.startTick,
          .sequence = voice.startSequence,
          .sourceCents = voice.sourceCents,
          .voiceCents = range,
      });
      activeRange = range;
    }
  }
  return changes;
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
  if (mode == LfoRestartMode::PhaseAndDelay) {
    lfo.delayCounterTicks = 0;
    lfo.delayCounterMilliseconds = 0.0;
  }
  lfo.cursorTick = tick;
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

// Renders one MIDI channel using its output track, controller state, and timing context.
class MidiTrackRenderer {
public:
  MidiTrackRenderer(MidiTrack& track, u8 channel, const PerformanceTrack& source, const PerformanceTempoMap& tempos,
                    const MidiExportOptions& options, double headroom)
      : track(track), channel(channel), options(options), tempos(tempos),
        notePredecessors(performanceNotePredecessors(source)), levelHeadroom(headroom) {
    // Use original limits: pitch lowering merges extensions and changes fragment ticks.
    for (const auto& event : source.events) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      if (note != nullptr && note->note.valid() && note->maximumDurationMilliseconds) {
        const u64 limit = physicalNoteEnd(*note, tempos);
        auto& end = sourceNoteEndLimits.try_emplace(note->note, limit).first->second;
        end = std::min(end, limit);
      }
    }
  }

  void render(const PerformanceTrack& lowered, const PerformanceTimeline& timeline,
              std::span<const GlobalTransposePerformanceEvent* const> globalTransposes,
              ModulationConversionPolicy modulationConversion, std::span<const SoundBankAsset* const> soundBanks,
              const SequenceModulationProfile* modulationProfile) {
    const auto pitchBendRangeChanges = planVoicePitchBendRanges(timeline, options.tuning, soundBanks);
    size_t nextPitchBendRangeChange = 0;
    applyInstrumentPitchBendRange(0, instrumentSelection(InstrumentAddress{}, soundBanks).pitchBendRangeCents,
                                  modulationConversion);
    for (const auto* event : timeline) {
      const auto& header = performanceEventHeader(*event);
      while (nextPitchBendRangeChange < pitchBendRangeChanges.size() &&
             std::pair{pitchBendRangeChanges[nextPitchBendRangeChange].tick,
                       pitchBendRangeChanges[nextPitchBendRangeChange].sequence} <= header.order()) {
        const auto& change = pitchBendRangeChanges[nextPitchBendRangeChange++];
        if (change.tick != 0) {
          // Finish the previous voice before changing the channel sensitivity.
          flushSimulatedVibrato(change.tick - 1);
        }
        applyVoicePitchBendRangeChange(change, modulationConversion);
      }

      const auto* note = std::get_if<NotePerformanceEvent>(event);
      flushSimulatedVibrato(header.tick, note);
      u64 otherFlushTick = header.tick;
      if (note != nullptr &&
          ((modulationConversion == ModulationConversionPolicy::SequenceEventSimulation &&
            shouldRestartSimulatedTremoloForNote(*note)) ||
           shouldRestartSimulatedPanForNote(*note)) &&
          otherFlushTick != 0) {
        --otherFlushTick;
      }
      if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
        flushSimulatedTremolo(otherFlushTick, modulationConversion);
      }
      flushSimulatedPan(otherFlushTick);
      addMidiEvent(*event, lowered.sourceTrackNumber, globalTransposes, modulationConversion, soundBanks,
                   modulationProfile);
    }
    flushSimulatedVibrato(lowered.endTick);
    if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
      flushSimulatedTremolo(lowered.endTick, modulationConversion);
    }
    flushSimulatedPan(lowered.endTick);
  }

private:
  MidiTrack& track;
  u8 channel;
  const MidiExportOptions& options;
  const PerformanceTempoMap& tempos;
  std::unordered_map<PerformanceNoteId, u64> sourceNoteEndLimits;
  std::unordered_map<PerformanceNoteId, PerformanceNoteId> notePredecessors;
  std::unordered_map<PerformanceNoteId, size_t> noteVoices;
  std::vector<RenderVoice> voices;
  std::optional<size_t> lastVoice;
  bool hasNote = false;
  // Source selection belongs to the next attack; MIDI may temporarily select
  // an older voice's preset for a native-portamento fragment.
  InstrumentSelection selectedInstrument;
  // MIDI starts in bank/program zero.
  u16 midiBank = 0;
  u8 midiProgram = 0;
  std::optional<u16> lastPitchBendRangeCents;
  PerformancePitchBendContext pitchBendContext;
  // Slides may replace the sequence range, but they must not reduce the range
  // required by the selected instrument.
  std::optional<u16> voicePitchBendRangeCents;
  double tuningSemitones = 0.0;
  PitchBendLayers pitchBendLayers;
  std::map<u32, SimulatedPitchLfoState> pitchLfos;
  std::optional<size_t> lastPitchBendIndex;
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

  RenderVoice& voiceForNote(const NotePerformanceEvent& note) {
    auto id = note.note;
    for (auto previous = notePredecessors.find(id); !noteVoices.contains(id) && previous != notePredecessors.end();
         previous = notePredecessors.find(id)) {
      id = previous->second;
    }
    const auto found = noteVoices.find(id);
    size_t voice = voices.size();
    if (found != noteVoices.end()) {
      voice = found->second;
    } else if (note.extendsPrevious && !notePredecessors.contains(note.note) && lastVoice) {
      voice = *lastVoice;
    }
    if (voice == voices.size()) {
      voices.emplace_back();
    }
    if (note.note.valid()) {
      noteVoices.emplace(note.note, voice);
    }
    lastVoice = voice;
    return voices[voice];
  }

  void addController(u64 tick, MidiController controller, s32 value, int priority = 20,
                     std::optional<double> normalizedAmount = std::nullopt) {
    track.events.push_back(midi::controller(tick, channel, controller, value, priority, normalizedAmount));
  }

  void addPitchBendRange(u64 tick, u16 cents) {
    const u8 semitones = static_cast<u8>(std::min<u16>(cents / 100, 127));
    const u8 fineCents = static_cast<u8>(std::min<u16>(cents % 100, 127));
    midi::appendRpn(track, tick, channel, 0, 0, static_cast<u16>((semitones << 7) | fineCents));
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

  [[nodiscard]] std::optional<u32> physicalNoteDuration(RenderVoice& voice, const NotePerformanceEvent& note) {
    const bool freshAttack = !note.extendsPrevious && note.restartsEnvelope;
    const auto sourceLimit = sourceNoteEndLimits.find(note.note);
    if (sourceLimit != sourceNoteEndLimits.end() || note.maximumDurationMilliseconds) {
      const u64 limit = sourceLimit != sourceNoteEndLimits.end() ? sourceLimit->second : physicalNoteEnd(note, tempos);
      if (!voice.endLimit || limit < *voice.endLimit) {
        voice.endLimit = limit;
        for (const size_t i : voice.fragments) {
          auto& event = track.events[i];
          auto& fragment = std::get<NoteDuration>(event.payload);
          fragment.duration = static_cast<u32>(std::min<u64>(fragment.duration, limit - std::min(limit, event.tick)));
        }
      }
    }
    if (!voice.endLimit) {
      return note.durationTicks;
    }
    if (!freshAttack && note.header.tick >= *voice.endLimit) {
      return std::nullopt;
    }
    return static_cast<u32>(std::min<u64>(note.durationTicks, *voice.endLimit - note.header.tick));
  }

  bool extendPreviousNote(RenderVoice& voice, const NotePerformanceEvent& note, u32 duration) {
    if (!note.extendsPrevious || voice.fragments.empty()) {
      return false;
    }

    MidiEvent& previousEvent = track.events[voice.fragments.back()];
    auto& previous = std::get<NoteDuration>(previousEvent.payload);
    const u64 previousEnd = previousEvent.tick + previous.duration;
    const u64 extensionEnd = note.header.tick + duration;
    if (extensionEnd > previousEnd) {
      previous.duration = static_cast<u32>(extensionEnd - previousEvent.tick);
    }
    return true;
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

  [[nodiscard]] u16 requiredPitchBendRangeCents() const {
    double voicePitch = tuningSemitones + layeredPitchBendSemitones();
    double maximumLfoExcursion = 0.0;
    for (const auto& entry : pitchLfos) {
      const auto& pitch = entry.second;
      const auto& lfo = pitch.oscillator;
      if (lfo.cyclesPerTick.value_or(lfo.frequencyHz) > 0.0) {
        maximumLfoExcursion += lfo.depth;
      } else {
        voicePitch += pitch.semitones;
      }
    }
    const double possibleSemitones = std::abs(voicePitch) + maximumLfoExcursion;
    const int cents = std::max<int>(200, static_cast<int>(std::ceil(possibleSemitones * 100.0)));
    return static_cast<u16>(std::min<int>(cents, std::numeric_limits<u16>::max()));
  }

  [[nodiscard]] u16 effectivePitchBendRangeCents(ModulationConversionPolicy modulationConversion) const {
    const u16 tuningRangeCents =
        tuningSemitones == 0.0
            ? 0
            : static_cast<u16>(std::clamp(std::ceil(std::abs(tuningSemitones + layeredPitchBendSemitones()) * 100.0),
                                          0.0, static_cast<double>(std::numeric_limits<u16>::max())));
    const u16 range = std::max({voicePitchBendRangeCents.value_or(pitchBendContext.sourceRangeCents()),
                                pitchBendContext.instrumentRangeCents().value_or(0), tuningRangeCents});
    const bool simulatesPitchLfo =
        modulationConversion == ModulationConversionPolicy::SequenceEventSimulation ||
        std::ranges::any_of(pitchLfos, [](const auto& entry) {
          return entry.first != kPrimaryPitchBendLayer.value && entry.second.oscillator.started;
        });
    return simulatesPitchLfo ? std::max(range, requiredPitchBendRangeCents()) : range;
  }

  [[nodiscard]] u16 ensurePitchBendRange(u64 tick, u16 cents) {
    const u16 range = wholeSemitonePitchBendRangeCents(cents);
    if (lastPitchBendRangeCents != range) {
      addPitchBendRange(tick, range);
      lastPitchBendRangeCents = range;
    }
    return range;
  }

  void addPitchBend(u64 tick, s16 value, bool force = false) {
    if (lastPitchBendIndex) {
      MidiEvent& previous = track.events[*lastPitchBendIndex];
      auto& message = std::get<MidiChannelMessage>(previous.payload);
      if (!force && message.value == value) {
        return;
      }
      if (previous.tick == tick) {
        message.value = value;
        return;
      }
    }
    lastPitchBendIndex = track.events.size();
    track.events.push_back(midi::pitchBend(tick, channel, value));
  }

  [[nodiscard]] double currentPitchBendSemitones() const {
    return tuningSemitones + layeredPitchBendSemitones() + simulatedPitchLfoSemitones();
  }

  void refreshPitchBendRange(u64 tick, u16 cents) {
    const u16 range = ensurePitchBendRange(tick, cents);
    if (lastPitchBendIndex) {
      // A source or instrument range can reinterpret a normalized layer even
      // when whole-semitone MIDI sensitivity remains unchanged.
      addPitchBend(tick, midiPitchBend(currentPitchBendSemitones(), range));
    }
  }

  void applyInstrumentPitchBendRange(u64 tick, std::optional<u16> cents,
                                     ModulationConversionPolicy modulationConversion) {
    if (pitchBendContext.instrumentRangeCents() == cents) {
      return;
    }
    const u16 previousRange = effectivePitchBendRangeCents(modulationConversion);
    pitchBendContext.setInstrumentRangeCents(cents);
    const u16 range = effectivePitchBendRangeCents(modulationConversion);
    if (!lastPitchBendIndex &&
        wholeSemitonePitchBendRangeCents(range) == wholeSemitonePitchBendRangeCents(previousRange)) {
      return;
    }
    refreshPitchBendRange(tick, range);
  }

  void applyInstrumentSelection(u64 tick, const MidiInstrumentSelection& selection,
                                ModulationConversionPolicy modulationConversion, bool forceProgramChange) {
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
    applyInstrumentPitchBendRange(tick, selection.pitchBendRangeCents, modulationConversion);
  }

  void applyVoicePitchBendRangeChange(const VoicePitchBendRangeChange& change,
                                      ModulationConversionPolicy modulationConversion) {
    pitchBendContext.setSourceRangeCents(change.sourceCents);
    voicePitchBendRangeCents = change.voiceCents;
    refreshPitchBendRange(change.tick, effectivePitchBendRangeCents(modulationConversion));
  }

  void addCurrentPitchBend(u64 tick, ModulationConversionPolicy modulationConversion, bool force = true) {
    const u16 range = ensurePitchBendRange(tick, effectivePitchBendRangeCents(modulationConversion));
    const s16 value = midiPitchBend(currentPitchBendSemitones(), range);
    addPitchBend(tick, value, force);
  }

  void flushSimulatedVibrato(u64 upToTick, const NotePerformanceEvent* note = nullptr) {
    const bool restartsPitch =
        note != nullptr && note->restartsVibratoLfoPhase.value_or(!note->extendsPrevious && note->restartsLfoPhase);
    const auto layerEnd = [&](const SimulatedPitchLfoState& pitch) {
      return restartsPitch && pitch.oscillator.restartsOnNote && upToTick != 0 ? upToTick - 1 : upToTick;
    };
    while (true) {
      std::optional<u64> nextTick;
      for (const auto& entry : pitchLfos) {
        const auto& pitch = entry.second;
        if (pitch.oscillator.cursorTick < layerEnd(pitch)) {
          nextTick = std::min(nextTick.value_or(pitch.oscillator.cursorTick + 1), pitch.oscillator.cursorTick + 1);
        }
      }
      if (!nextTick) {
        break;
      }
      bool sampled = false;
      for (auto& entry : pitchLfos) {
        auto& pitch = entry.second;
        flushLfo(pitch.oscillator, std::min(*nextTick, layerEnd(pitch)), tempos, [&](u64, double value) {
          pitch.semitones = simulatedVibratoAtPhase(pitch.oscillator, value);
          sampled = true;
        });
      }
      if (sampled) {
        addCurrentPitchBend(*nextTick, ModulationConversionPolicy::SequenceEventSimulation, false);
      }
    }
  }

  void setSimulatedVibratoDepth(u64 tick, double semitones, LfoZeroDepthBehavior zeroDepthBehavior,
                                PitchBendLayerId layer) {
    auto& pitch = pitchLfo(layer);
    auto& lfo = pitch.oscillator;
    lfo.depth = std::max(0.0, semitones);
    lfo.outputHeldUntilNextNote =
        lfo.depth <= 0.0 && zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote;
    if (lfo.depth <= 0.0 && !lfo.outputHeldUntilNextNote) {
      pitch.semitones = 0.0;
      addCurrentPitchBend(tick, ModulationConversionPolicy::SequenceEventSimulation, false);
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
    if (value != pitch.semitones) {
      pitch.semitones = value;
      addCurrentPitchBend(event.header.tick, ModulationConversionPolicy::SequenceEventSimulation, false);
    }
  }

  void restartSimulatedVibratoForNote(u64 tick) {
    bool changed = false;
    for (auto& entry : pitchLfos) {
      auto& pitch = entry.second;
      auto& lfo = pitch.oscillator;
      if (!lfo.started || !lfo.restartsOnNote) {
        continue;
      }
      restartNoteLfo(lfo, tick);
      lfo.outputHeldUntilNextNote = false;
      const double previousSemitones = pitch.semitones;
      const bool startsImmediately = lfo.canSampleImmediately();
      pitch.semitones = startsImmediately ? simulatedVibratoAtPhase(lfo, lfoValue(lfo)) : 0.0;
      lfo.producedSample = startsImmediately;
      changed |= startsImmediately || previousSemitones != 0.0;
    }
    if (changed) {
      addCurrentPitchBend(tick, ModulationConversionPolicy::SequenceEventSimulation, false);
    }
  }

  bool shouldRestartSimulatedVibratoForNote(const NotePerformanceEvent& note) const {
    if (!note.restartsVibratoLfoPhase.value_or(!note.extendsPrevious && note.restartsLfoPhase)) {
      return false;
    }
    return std::ranges::any_of(pitchLfos, [](const auto& entry) {
      return entry.second.oscillator.started && entry.second.oscillator.restartsOnNote;
    });
  }

  void releaseHeldPitchLfoOutputs(u64 tick) {
    bool changed = false;
    for (auto& entry : pitchLfos) {
      auto& pitch = entry.second;
      if (releaseHeldLfoOutput(pitch.oscillator) && pitch.semitones != 0.0) {
        pitch.semitones = 0.0;
        changed = true;
      }
    }
    if (changed) {
      addCurrentPitchBend(tick, ModulationConversionPolicy::SequenceEventSimulation, false);
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

  bool shouldRestartSimulatedTremoloForNote(const NotePerformanceEvent& note) const {
    return note.restartsTremoloLfoPhase.value_or(!note.extendsPrevious && note.restartsLfoPhase) && tremolo.started;
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

  bool shouldRestartSimulatedPanForNote(const NotePerformanceEvent& note) const {
    return !note.extendsPrevious && note.restartsLfoPhase && panLfo.started;
  }

  void addMidiEvent(const PerformanceEvent& event, u32 sourceTrackNumber,
                    std::span<const GlobalTransposePerformanceEvent* const> globalTransposes,
                    ModulationConversionPolicy modulationConversion, std::span<const SoundBankAsset* const> soundBanks,
                    const SequenceModulationProfile* modulationProfile) {
    const bool forceControllers = !performanceEventHeader(event).automation;
    std::visit(
        [&](const auto& typedEvent) {
          using TypedEvent = std::decay_t<decltype(typedEvent)>;
          if constexpr (std::is_same_v<TypedEvent, NotePerformanceEvent>) {
            auto& voice = voiceForNote(typedEvent);
            const auto duration = physicalNoteDuration(voice, typedEvent);
            if (!duration) {
              return;
            }
            if (!typedEvent.extendsPrevious) {
              const auto selection = typedEvent.instrumentAddress ? InstrumentSelection{*typedEvent.instrumentAddress}
                                                                  : selectedInstrument;
              auto resolved = instrumentSelection(selection, soundBanks);
              resolved.forceBankSelect = false;
              applyInstrumentSelection(typedEvent.header.tick, resolved, modulationConversion, false);
            }
            const u8 key = midiKey(typedEvent.key + globalTransposeAt(globalTransposes, typedEvent.header.tick));
            if (shouldRestartSimulatedVibratoForNote(typedEvent)) {
              restartSimulatedVibratoForNote(typedEvent.header.tick);
            } else {
              releaseHeldPitchLfoOutputs(typedEvent.header.tick);
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
            if (extendPreviousNote(voice, typedEvent, *duration)) {
              return;
            }
            if (options.terminatePreviousVoice && typedEvent.restartsEnvelope && hasNote) {
              // Silence the old voice before bank, range, controller, or bend
              // state for this attack can affect it.
              addController(typedEvent.header.tick, MidiController::AllSoundOff, 0, kVoiceTerminationPriority);
            }
            if (!lastVolume && (levelHeadroom != 1.0 || sourceLevelGain != 1.0 || panLevelGain != 1.0)) {
              addCombinedLevel(typedEvent.header.tick);
            }
            hasNote = true;
            voice.fragments.push_back(track.events.size());
            track.events.push_back(
                midi::note(typedEvent.header.tick, channel, key, midiVelocity(typedEvent.linearVelocity), *duration));
          } else if constexpr (std::is_same_v<TypedEvent, TempoPerformanceEvent>) {
            // Tempo is song-wide. Effective changes are written once on the
            // first MIDI track after all source tracks have been lowered.
          } else if constexpr (std::is_same_v<TypedEvent, TimeSignaturePerformanceEvent>) {
            // Standard MIDI treats time signatures as global metadata. They are collected
            // once and written to the first MIDI track by renderMidiSequence.
          } else if constexpr (std::is_same_v<TypedEvent, InstrumentPerformanceEvent>) {
            selectedInstrument = typedEvent.instrument;
            const auto selection = instrumentSelection(typedEvent.instrument, soundBanks, typedEvent.forceBankSelect);
            applyInstrumentSelection(typedEvent.header.tick, selection, modulationConversion, true);
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
            const double bend = tuningBendSemitones(typedEvent.cents, options.tuning);
            if (bend != tuningSemitones) {
              tuningSemitones = bend;
              addCurrentPitchBend(typedEvent.header.tick, modulationConversion, false);
            }
          } else if constexpr (std::is_same_v<TypedEvent, GlobalTransposePerformanceEvent>) {
            // Global transpose changes how later notes and portamento controls are written. It does not
            // become a MIDI event itself.
          } else if constexpr (std::is_same_v<TypedEvent, PitchBendPerformanceEvent>) {
            pitchBendLayers.apply(typedEvent);
            addCurrentPitchBend(typedEvent.header.tick, modulationConversion, false);
          } else if constexpr (std::is_same_v<TypedEvent, PitchBendRangePerformanceEvent>) {
            pitchBendContext.setSourceRangeCents(typedEvent.cents);
            refreshPitchBendRange(typedEvent.header.tick, effectivePitchBendRangeCents(modulationConversion));
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
                    typedEvent.header.tick,
                    typedEvent.pitchDepthSemitones.value_or(std::clamp(typedEvent.amount, 0.0, 1.0) * 2.0),
                    typedEvent.context.zeroDepthBehavior, typedEvent.pitchLayer);
              }
              refreshPitchBendRange(typedEvent.header.tick, effectivePitchBendRangeCents(modulationConversion));
              updateRestartedVibratoOutput(typedEvent);
              if (typedEvent.target == ModulationPerformanceTarget::VibratoRate && lastPitchBendIndex) {
                addCurrentPitchBend(typedEvent.header.tick, modulationConversion, false);
              }
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

MidiSequence renderMidiSequence(const PerformanceSequence& performance, MidiExportOptions options,
                                ModulationConversionPolicy modulationConversion,
                                std::span<const SoundBankAsset* const> soundBanks,
                                const SequenceModulationProfile* modulationProfile) {
  std::optional<SequenceModulationProfile> derivedModulationProfile;
  if (modulationProfile == nullptr) {
    derivedModulationProfile = analyzeSequenceModulation(performance);
    modulationProfile = &*derivedModulationProfile;
  }

  const PerformanceTempoMap globalTempos{performance};
  const std::vector<PerformanceTempoMap::Point> globalTempoPoints = globalTempos.points();
  const PerformanceSequence loweredPerformance =
      lowerMidiPerformanceAutomation(performance, options, globalTempos, soundBanks);
  MidiSequence sequence{
      .timebase = loweredPerformance.timebase,
      .diagnostics = loweredPerformance.diagnostics,
  };
  sequence.tracks.reserve(loweredPerformance.tracks.size());
  const PerformanceTimelines timelines = buildPerformanceTimelines(loweredPerformance);
  const auto globalTransposes = orderedPerformanceEvents<GlobalTransposePerformanceEvent>(loweredPerformance);
  const auto globalTimeSignatures = orderedPerformanceEvents<TimeSignaturePerformanceEvent>(loweredPerformance);
  const double levelHeadroom = panLevelHeadroom(timelines);

  for (size_t trackIndex = 0; trackIndex < loweredPerformance.tracks.size(); ++trackIndex) {
    const auto& performanceTrack = loweredPerformance.tracks[trackIndex];
    MidiTrack midiTrack{
        .name = performanceTrack.name.empty() ? "Track " + std::to_string(performanceTrack.sourceTrackNumber)
                                              : performanceTrack.name,
    };
    const auto assignment = midiChannelAssignment(trackIndex, options);
    MidiTrackRenderer renderer{midiTrack, assignment.channel, performance.tracks[trackIndex],
                               globalTempos, options, levelHeadroom};
    if (assignment.port > 255) {
      sequence.diagnostics.push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "MIDI port number exceeded the Standard MIDI File port meta-event range",
      });
    }
    if (options.writePortMetaEvents) {
      midiTrack.events.push_back(midi::meta(0, 0x21, {midiPortByte(assignment.port)}, -5));
    }
    renderer.render(performanceTrack, timelines[trackIndex], globalTransposes, modulationConversion, soundBanks,
                    modulationProfile);
    u64 endTick = performanceTrack.endTick;
    if (trackIndex == 0) {
      for (const auto& tempo : globalTempoPoints) {
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

  return sequence;
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PerformanceMidiRenderer.h"

#include "value/base/LevelScale.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u8 data7(double value) {
  return static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 127));
}

[[nodiscard]] u16 data14(double value) {
  return static_cast<u16>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 16383));
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
  double expressionGain = 1.0;
};

[[nodiscard]] LoweredStereoBalance lowerStereoBalance(double sourceLeft, double sourceRight) {
  constexpr double piOverTwo = 1.57079632679489661923;
  sourceLeft = std::max(0.0, sourceLeft);
  sourceRight = std::max(0.0, sourceRight);

  // MIDI pan has only one position value, while source engines may specify two
  // independent channel gains. Pick the closest equal-power MIDI position,
  // then use expression to retain the source engine's combined loudness.
  u8 pan = 64;
  if (sourceLeft == 0.0 && sourceRight == 0.0) {
    pan = 64;
  } else if (sourceRight == 0.0) {
    pan = 0;
  } else if (sourceLeft == sourceRight) {
    pan = 64;
  } else if (sourceLeft == 0.0) {
    pan = 127;
  } else {
    const double arcPosition = std::atan2(sourceRight, sourceLeft) / piOverTwo;
    pan = static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(arcPosition * 126.0)), 0, 126));
    if (pan != 0) {
      ++pan;
    }
  }

  double midiLeft = 0.0;
  double midiRight = 0.0;
  if (pan == 0 || pan == 1) {
    midiLeft = 1.0;
  } else if (pan == 64) {
    midiLeft = std::sqrt(2.0) / 2.0;
    midiRight = midiLeft;
  } else if (pan == 127) {
    midiRight = 1.0;
  } else {
    const double arcPosition = (pan - 1) / 126.0;
    midiLeft = std::cos(piOverTwo * arcPosition);
    midiRight = std::sin(piOverTwo * arcPosition);
  }

  const double midiGain = midiLeft + midiRight;
  return LoweredStereoBalance{
      .pan = pan,
      .expressionGain = midiGain == 0.0 ? 0.0 : (sourceLeft + sourceRight) / midiGain,
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
          .expressionGain = 1.0,
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

[[nodiscard]] MidiLevelResolution resolveLevelResolution(MidiLevelResolution requested, LevelPrecisionHint hint,
                                                         std::optional<ValueQuantization> quantization = std::nullopt) {
  if (requested != MidiLevelResolution::Auto) {
    return requested;
  }
  if (quantization && quantization->levels > 128) {
    return MidiLevelResolution::FourteenBit;
  }
  return hint == LevelPrecisionHint::FourteenBit ? MidiLevelResolution::FourteenBit : MidiLevelResolution::SevenBit;
}

[[nodiscard]] bool writeBankSelectLsb(const MidiExportOptions& options) {
  return options.bankSelectStyle == MidiBankSelectStyle::MsbAndLsb;
}

struct MidiControllerState {
  std::optional<u8> volume7;
  std::optional<u16> volume14;
  std::optional<u8> expression7;
  std::optional<u16> expression14;
  std::optional<u8> pan;
};

void addVolume(MidiTrack& track, MidiControllerState* state, u64 tick, u8 channel, double linearGain,
               LevelPrecisionHint precisionHint, const MidiExportOptions& options,
               std::optional<ValueQuantization> quantization = std::nullopt) {
  if (resolveLevelResolution(options.volumeResolution, precisionHint, quantization) ==
      MidiLevelResolution::FourteenBit) {
    const u16 value = LevelScale::midi14FromLinear(linearGain);
    if (state != nullptr && state->volume14 && *state->volume14 == value) {
      return;
    }
    track.events.push_back(Volume14{.tick = tick, .channel = channel, .value = value});
    if (state != nullptr) {
      state->volume14 = value;
      state->volume7.reset();
    }
  } else {
    const u8 value = LevelScale::midi7FromLinear(linearGain);
    if (state != nullptr && state->volume7 && *state->volume7 == value) {
      return;
    }
    track.events.push_back(Volume{.tick = tick, .channel = channel, .value = value});
    if (state != nullptr) {
      state->volume7 = value;
      state->volume14.reset();
    }
  }
}

void addExpression(MidiTrack& track, MidiControllerState* state, u64 tick, u8 channel, double linearGain,
                   LevelPrecisionHint precisionHint, const MidiExportOptions& options,
                   std::optional<ValueQuantization> quantization = std::nullopt) {
  if (resolveLevelResolution(options.expressionResolution, precisionHint, quantization) ==
      MidiLevelResolution::FourteenBit) {
    const u16 value = LevelScale::midi14FromLinear(linearGain);
    if (state != nullptr && state->expression14 && *state->expression14 == value) {
      return;
    }
    track.events.push_back(Expression14{.tick = tick, .channel = channel, .value = value});
    if (state != nullptr) {
      state->expression14 = value;
      state->expression7.reset();
    }
  } else {
    const u8 value = LevelScale::midi7FromLinear(linearGain);
    if (state != nullptr && state->expression7 && *state->expression7 == value) {
      return;
    }
    track.events.push_back(Expression{.tick = tick, .channel = channel, .value = value});
    if (state != nullptr) {
      state->expression7 = value;
      state->expression14.reset();
    }
  }
}

void addPan(MidiTrack& track, MidiControllerState* state, u64 tick, u8 channel, u8 value) {
  if (state != nullptr && state->pan && *state->pan == value) {
    return;
  }
  track.events.push_back(Pan{.tick = tick, .channel = channel, .value = value});
  if (state != nullptr) {
    state->pan = value;
  }
}

struct MidiInstrumentSelection {
  InstrumentAddress address;
  bool forceBankSelect = false;
  // Direct performance events already use MIDI's packed 14-bit bank space.
  // Addresses resolved from synth instruments are logical preset banks and
  // still need to be lowered for the selected MIDI bank convention.
  bool logicalBank = false;
};

[[nodiscard]] MidiInstrumentSelection instrumentSelection(const InstrumentPerformanceEvent& event,
                                                          std::span<const InstrumentSetAsset* const> instrumentSets) {
  if (!event.sourceInstrument) {
    return MidiInstrumentSelection{
        .address = resolveInstrumentAddress(InstrumentAddress{.bank = event.bank, .program = event.program}, {}),
        .forceBankSelect = event.forceBankSelect,
        .logicalBank = false,
    };
  }

  for (const auto* instrumentSet : instrumentSets) {
    if (instrumentSet == nullptr) {
      continue;
    }
    const auto found = std::ranges::find_if(instrumentSet->instruments, [&](const Instrument& instrument) {
      return instrument.identity && *instrument.identity == *event.sourceInstrument;
    });
    if (found != instrumentSet->instruments.end()) {
      return MidiInstrumentSelection{
          .address = resolveInstrumentAddress(found->explicitAddress, found->identity),
          .forceBankSelect = true,
          .logicalBank = true,
      };
    }
  }

  // A sequential key is a deterministic fallback for incomplete collections.
  // This is an export policy, not a bank convention imposed on format code.
  return MidiInstrumentSelection{
      .address = resolveInstrumentAddress({}, event.sourceInstrument),
      .forceBankSelect = true,
      .logicalBank = true,
  };
}

[[nodiscard]] u16 midiBank(const MidiInstrumentSelection& selection, const MidiExportOptions& options) {
  if (!selection.logicalBank) {
    return static_cast<u16>(selection.address.bank);
  }
  if (options.bankSelectStyle == MidiBankSelectStyle::MsbOnly) {
    return static_cast<u16>((selection.address.bank & 0x7f) << 7);
  }
  return static_cast<u16>(selection.address.bank & 0x3fff);
}

struct SimulatedLfoState {
  double depth = 0.0;
  double frequencyHz = 0.0;
  std::optional<double> cyclesPerTick;
  u32 delayTicks = 0;
  std::optional<double> delayMilliseconds;
  bool delayIsTempoRelative = false;
  u32 delayCounterTicks = 0;
  double delayCounterMilliseconds = 0.0;
  u64 cursorTick = 0;
  double phaseCycles = 0.0;
  std::optional<LfoWaveform> waveform;
  std::optional<double> initialPhaseCycles;
  std::optional<ModulationRange> pitchRangeSemitones;
  u32 steppedDepthAttackSteps = 0;
  u32 activeSteppedDepthAttackSteps = 0;
  u32 steppedDepthAttackStep = 0;
  double steppedDepthAttackPhaseCycles = 0.0;
  bool sampleImmediatelyOnNote = false;
  bool phaseRunsAtZeroDepth = false;
  bool configured = false;
  bool started = false;
  bool producedSample = false;
};

struct RenderTrackState {
  std::optional<size_t> lastNoteIndex;
  u16 pitchBendRangeCents = 200;
  std::optional<u16> lastPitchBendRangeCents;
  u16 sourcePitchBendRangeCents = 200;
  double sourcePitchBendSemitones = 0.0;
  double simulatedVibratoSemitones = 0.0;
  SimulatedLfoState vibrato;
  std::optional<s16> lastPitchBendValue;
  double sourceExpressionGain = 1.0;
  LevelPrecisionHint sourceExpressionPrecisionHint = LevelPrecisionHint::SevenBit;
  std::optional<ValueQuantization> sourceExpressionQuantization;
  double panExpressionGain = 1.0;
  double simulatedTremoloGain = 1.0;
  bool tremoloDepthIsDecibels = false;
  TremoloGainMode tremoloGainMode = TremoloGainMode::BipolarAroundNominal;
  SimulatedLfoState tremolo;
  double sourcePanPosition = 0.0;
  double simulatedPanOffset = 0.0;
  std::optional<u8> lastPanValue;
  SimulatedLfoState panLfo;
};

struct GlobalTransposeChange {
  u64 tick = 0;
  s32 semitones = 0;
  size_t sequence = 0;
};

using PerformanceTimeline = std::vector<const PerformanceEvent*>;
using PerformanceTimelines = std::vector<PerformanceTimeline>;

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
    std::ranges::stable_sort(timeline, [](const PerformanceEvent* lhs, const PerformanceEvent* rhs) {
      const auto& left = performanceEventHeader(*lhs);
      const auto& right = performanceEventHeader(*rhs);
      return std::tie(left.tick, left.sequence) < std::tie(right.tick, right.sequence);
    });
  }
  return timelines;
}

[[nodiscard]] std::vector<GlobalTransposeChange> collectGlobalTransposeChanges(const PerformanceTimelines& timelines) {
  std::vector<GlobalTransposeChange> changes;
  for (const auto& timeline : timelines) {
    for (const auto* event : timeline) {
      const auto* transpose = std::get_if<GlobalTransposePerformanceEvent>(event);
      if (transpose == nullptr) {
        continue;
      }
      changes.push_back(GlobalTransposeChange{
          .tick = transpose->header.tick,
          .semitones = transpose->semitones,
          .sequence = changes.size(),
      });
    }
  }
  std::ranges::stable_sort(changes, [](const GlobalTransposeChange& lhs, const GlobalTransposeChange& rhs) {
    return std::tie(lhs.tick, lhs.sequence) < std::tie(rhs.tick, rhs.sequence);
  });
  return changes;
}

[[nodiscard]] s32 globalTransposeAt(std::span<const GlobalTransposeChange> changes, u64 tick) {
  s32 semitones = 0;
  for (const auto& change : changes) {
    if (change.tick > tick) {
      break;
    }
    semitones = change.semitones;
  }
  return semitones;
}

[[nodiscard]] std::vector<TimeSignature> collectGlobalTimeSignatures(const PerformanceTimelines& timelines) {
  std::vector<TimeSignature> timeSignatures;
  for (const auto& timeline : timelines) {
    for (const auto* event : timeline) {
      const auto* timeSignature = std::get_if<TimeSignaturePerformanceEvent>(event);
      if (timeSignature == nullptr) {
        continue;
      }
      timeSignatures.push_back(TimeSignature{
          .tick = timeSignature->header.tick,
          .numerator = timeSignature->numerator,
          .denominator = timeSignature->denominator,
          .clocksPerMetronomeClick = timeSignature->clocksPerMetronomeClick,
      });
    }
  }
  std::ranges::stable_sort(timeSignatures,
                           [](const TimeSignature& lhs, const TimeSignature& rhs) { return lhs.tick < rhs.tick; });
  return timeSignatures;
}

bool extendPreviousNote(MidiTrack& track, RenderTrackState& state, const NotePerformanceEvent& note, u8 channel) {
  if (!note.extendsPrevious || !state.lastNoteIndex || *state.lastNoteIndex >= track.events.size()) {
    return false;
  }

  auto* previous = std::get_if<NoteDuration>(&track.events[*state.lastNoteIndex]);
  if (previous == nullptr || previous->channel != channel) {
    return false;
  }

  const u64 previousEnd = previous->tick + previous->duration;
  const u64 extensionEnd = note.header.tick + note.durationTicks;
  if (extensionEnd > previousEnd) {
    previous->duration = static_cast<u32>(extensionEnd - previous->tick);
  }
  return true;
}

[[nodiscard]] u16 requiredPitchBendRangeCents(const RenderTrackState& state) {
  const bool vibratoPhaseAdvances = state.vibrato.cyclesPerTick.value_or(state.vibrato.frequencyHz) > 0.0;
  const double possibleSemitones = vibratoPhaseAdvances
                                       ? std::abs(state.sourcePitchBendSemitones) + state.vibrato.depth
                                       : std::abs(state.sourcePitchBendSemitones + state.simulatedVibratoSemitones);
  const int cents = std::max<int>(200, static_cast<int>(std::ceil(possibleSemitones * 100.0)));
  return static_cast<u16>(std::min<int>(cents, std::numeric_limits<u16>::max()));
}

[[nodiscard]] u16 wholeSemitonePitchBendRangeCents(u16 cents) {
  constexpr u32 kMinimumRangeCents = 200;
  constexpr u32 kMaximumWholeSemitoneRangeCents = 12'700;
  const u32 clamped = std::clamp<u32>(cents, kMinimumRangeCents, kMaximumWholeSemitoneRangeCents);
  return static_cast<u16>(((clamped + 99) / 100) * 100);
}

void ensurePitchBendRange(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, u16 cents) {
  const u16 range = wholeSemitonePitchBendRangeCents(cents);
  if (state.lastPitchBendRangeCents && *state.lastPitchBendRangeCents == range) {
    state.pitchBendRangeCents = range;
    return;
  }
  track.events.push_back(PitchBendRange{
      .tick = tick,
      .channel = channel,
      .cents = range,
  });
  state.pitchBendRangeCents = range;
  state.lastPitchBendRangeCents = range;
}

void addPitchBend(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, s16 value, bool force = false) {
  if (!force && state.lastPitchBendValue && *state.lastPitchBendValue == value) {
    return;
  }
  track.events.push_back(PitchBend{
      .tick = tick,
      .channel = channel,
      .value = value,
  });
  state.lastPitchBendValue = value;
}

void addCombinedPitchBend(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, bool force = true) {
  ensurePitchBendRange(track, state, tick, channel,
                       std::max(state.sourcePitchBendRangeCents, requiredPitchBendRangeCents(state)));
  const s16 value =
      midiPitchBend(state.sourcePitchBendSemitones + state.simulatedVibratoSemitones, state.pitchBendRangeCents);
  addPitchBend(track, state, tick, channel, value, force);
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
  }
  return 0.0;
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

void restartLfo(SimulatedLfoState& lfo, u64 tick, LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  lfo.delayCounterTicks = 0;
  lfo.delayCounterMilliseconds = 0.0;
  lfo.cursorTick = tick;
  lfo.phaseCycles = initialLfoPhase(lfo, fallback);
  lfo.activeSteppedDepthAttackSteps = lfo.steppedDepthAttackSteps;
  lfo.steppedDepthAttackStep = lfo.activeSteppedDepthAttackSteps == 0 ? 0 : 1;
  lfo.steppedDepthAttackPhaseCycles = 0.0;
  lfo.started = true;
  lfo.producedSample = false;
}

void configureLfo(SimulatedLfoState& lfo, u64 tick, const ModulationPerformanceEvent& event,
                  LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  if (event.cyclesPerTick) {
    lfo.cyclesPerTick = std::max(0.0, *event.cyclesPerTick);
  } else if (event.frequencyHz) {
    lfo.cyclesPerTick.reset();
  }
  if (event.frequencyHz) {
    lfo.frequencyHz = std::max(0.0, *event.frequencyHz);
  }
  if (event.waveform) {
    lfo.waveform = event.waveform;
  }
  if (event.initialPhaseCycles) {
    lfo.initialPhaseCycles = event.initialPhaseCycles;
  }
  if (event.pitchRangeSemitones) {
    lfo.pitchRangeSemitones = event.pitchRangeSemitones;
  }
  if (event.steppedDepthAttackSteps) {
    lfo.steppedDepthAttackSteps = *event.steppedDepthAttackSteps;
  }
  lfo.sampleImmediatelyOnNote = event.sampleImmediatelyOnNote;
  if (event.delayTicks) {
    lfo.delayTicks = *event.delayTicks;
  }
  if (event.delayMilliseconds) {
    lfo.delayMilliseconds = std::max(0.0, *event.delayMilliseconds);
  }
  if (event.delayTicks || event.delayMilliseconds) {
    lfo.delayIsTempoRelative = event.delayIsTempoRelative;
  }
  lfo.phaseRunsAtZeroDepth = event.phaseRunsAtZeroDepth;
  lfo.configured = true;
  if (!lfo.started) {
    restartLfo(lfo, tick, fallback);
  }
}

void setLfoDelay(SimulatedLfoState& lfo, u64 tick, u32 delayTicks, std::optional<double> delayMilliseconds,
                 bool tempoRelative, LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  lfo.delayTicks = delayTicks;
  lfo.delayMilliseconds = delayMilliseconds;
  lfo.delayIsTempoRelative = tempoRelative;
  lfo.configured = true;
  if (!lfo.started) {
    restartLfo(lfo, tick, fallback);
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
    if (!lfo.delayIsTempoRelative && lfo.delayMilliseconds) {
      if (lfo.delayCounterMilliseconds < *lfo.delayMilliseconds) {
        lfo.delayCounterMilliseconds =
            std::min(*lfo.delayMilliseconds, lfo.delayCounterMilliseconds + tickMilliseconds);
        if (lfo.delayCounterMilliseconds < *lfo.delayMilliseconds) {
          continue;
        }
      }
    } else if (lfo.delayCounterTicks < lfo.delayTicks) {
      ++lfo.delayCounterTicks;
      if (lfo.delayCounterTicks < lfo.delayTicks) {
        continue;
      }
    }

    if (lfo.cyclesPerTick.value_or(lfo.frequencyHz) <= 0.0 || (lfo.depth <= 0.0 && !lfo.phaseRunsAtZeroDepth)) {
      continue;
    }

    const double phaseStep = lfo.cyclesPerTick.value_or(lfo.frequencyHz * tickSeconds);
    const auto advancePhase = [&]() {
      lfo.phaseCycles = std::fmod(lfo.phaseCycles + phaseStep, 1.0);
      if (lfo.activeSteppedDepthAttackSteps != 0 && lfo.steppedDepthAttackStep < lfo.activeSteppedDepthAttackSteps) {
        const double attackPhase = lfo.steppedDepthAttackPhaseCycles + phaseStep;
        const u32 completedCycles = static_cast<u32>(std::floor(attackPhase));
        lfo.steppedDepthAttackPhaseCycles = attackPhase - std::floor(attackPhase);
        lfo.steppedDepthAttackStep =
            std::min(lfo.activeSteppedDepthAttackSteps, lfo.steppedDepthAttackStep + completedCycles);
      }
    };
    if (lfo.producedSample && lfo.sampleImmediatelyOnNote) {
      advancePhase();
    }
    const double value = lfoValue(lfo.waveform.value_or(LfoWaveform::Triangle), lfo.phaseCycles);
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

void flushSimulatedVibrato(MidiTrack& track, RenderTrackState& state, u64 upToTick, u8 channel,
                           const PerformanceTempoMap& tempos) {
  flushLfo(state.vibrato, upToTick, tempos, [&](u64 tick, double value) {
    state.simulatedVibratoSemitones = simulatedVibratoAtPhase(state.vibrato, value);
    addCombinedPitchBend(track, state, tick, channel, false);
  });
}

void setSimulatedVibratoDepth(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, double semitones) {
  state.vibrato.depth = std::max(0.0, semitones);
  if (state.vibrato.depth <= 0.0) {
    state.simulatedVibratoSemitones = 0.0;
    addCombinedPitchBend(track, state, tick, channel, false);
  }
}

void restartSimulatedVibratoForNote(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel) {
  if (!state.vibrato.configured) {
    return;
  }

  restartLfo(state.vibrato, tick);
  const double previousSemitones = state.simulatedVibratoSemitones;
  const bool startsImmediately = state.vibrato.sampleImmediatelyOnNote && state.vibrato.delayTicks == 0 &&
                                 state.vibrato.delayMilliseconds.value_or(0.0) <= 0.0 &&
                                 state.vibrato.cyclesPerTick.value_or(state.vibrato.frequencyHz) > 0.0 &&
                                 state.vibrato.depth > 0.0;
  state.simulatedVibratoSemitones =
      startsImmediately
          ? simulatedVibratoAtPhase(state.vibrato, lfoValue(state.vibrato.waveform.value_or(LfoWaveform::Triangle),
                                                            state.vibrato.phaseCycles))
          : 0.0;
  state.vibrato.producedSample = startsImmediately;
  if (startsImmediately || previousSemitones != 0.0) {
    addCombinedPitchBend(track, state, tick, channel, false);
  }
}

bool shouldRestartSimulatedVibratoForNote(const PerformanceEvent& event, const RenderTrackState& state) {
  const auto* note = std::get_if<NotePerformanceEvent>(&event);
  return note != nullptr && note->restartsVibratoLfoPhase.value_or(!note->extendsPrevious && note->restartsLfoPhase) &&
         state.vibrato.configured;
}

// Source expression, pan-law compensation, and simulated tremolo all use MIDI
// expression. Write their product so changing one cannot erase the others.
void addCombinedExpression(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                           const MidiExportOptions& options, ModulationConversionPolicy modulationConversion,
                           MidiControllerState* automationState = nullptr) {
  const bool simulatingTremolo = modulationConversion == ModulationConversionPolicy::SequenceEventSimulation;
  addExpression(track, automationState, tick, channel,
                state.sourceExpressionGain * state.panExpressionGain * state.simulatedTremoloGain,
                simulatingTremolo ? LevelPrecisionHint::SevenBit : state.sourceExpressionPrecisionHint, options,
                simulatingTremolo ? std::nullopt : state.sourceExpressionQuantization);
}

[[nodiscard]] double simulatedTremoloGain(const RenderTrackState& state, double lfoValue) {
  if (state.tremoloDepthIsDecibels) {
    double gainDecibels = state.tremolo.depth * lfoValue;
    if (state.tremoloGainMode == TremoloGainMode::NoBoost) {
      gainDecibels -= state.tremolo.depth;
    }
    return std::pow(10.0, gainDecibels / 20.0);
  }

  const double normalizedLfo = (lfoValue + 1.0) / 2.0;
  return 1.0 - (state.tremolo.depth * normalizedLfo);
}

void flushSimulatedTremolo(MidiTrack& track, RenderTrackState& state, u64 upToTick, u8 channel,
                           const PerformanceTempoMap& tempos, const MidiExportOptions& options,
                           ModulationConversionPolicy modulationConversion) {
  flushLfo(state.tremolo, upToTick, tempos, [&](u64 tick, double value) {
    state.simulatedTremoloGain = simulatedTremoloGain(state, value);
    addCombinedExpression(track, state, tick, channel, options, modulationConversion);
  });
}

[[nodiscard]] double tremoloGainAtCurrentPhase(const RenderTrackState& state) {
  if (state.tremolo.depth <= 0.0) {
    return 1.0;
  }

  const double value = lfoValue(state.tremolo.waveform.value_or(LfoWaveform::Triangle), state.tremolo.phaseCycles);
  return simulatedTremoloGain(state, value);
}

void setSimulatedTremoloDepth(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, double depth,
                              bool decibels, TremoloGainMode gainMode, const MidiExportOptions& options,
                              ModulationConversionPolicy modulationConversion) {
  state.tremolo.depth = std::max(0.0, depth);
  state.tremoloDepthIsDecibels = decibels;
  state.tremoloGainMode = gainMode;
  const double gain = tremoloGainAtCurrentPhase(state);
  if (gain != state.simulatedTremoloGain) {
    state.simulatedTremoloGain = gain;
    addCombinedExpression(track, state, tick, channel, options, modulationConversion);
  }
}

void restartSimulatedTremoloForNote(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                                    const MidiExportOptions& options, ModulationConversionPolicy modulationConversion) {
  if (!state.tremolo.configured) {
    return;
  }

  const auto fallback = !state.tremolo.waveform && !state.tremoloDepthIsDecibels
                            ? LfoInitialPhaseFallback::UnipolarTremoloNominalGain
                            : LfoInitialPhaseFallback::Zero;
  restartLfo(state.tremolo, tick, fallback);
  const double gain = tremoloGainAtCurrentPhase(state);
  if (gain != state.simulatedTremoloGain) {
    state.simulatedTremoloGain = gain;
    addCombinedExpression(track, state, tick, channel, options, modulationConversion);
  }
}

bool shouldRestartSimulatedTremoloForNote(const PerformanceEvent& event, const RenderTrackState& state) {
  const auto* note = std::get_if<NotePerformanceEvent>(&event);
  return note != nullptr && note->restartsTremoloLfoPhase.value_or(!note->extendsPrevious && note->restartsLfoPhase) &&
         state.tremolo.configured;
}

void addCombinedPan(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                    MidiControllerState* automationState = nullptr, bool force = false) {
  const u8 value = midiPan(state.sourcePanPosition + state.simulatedPanOffset);
  if (automationState == nullptr && !force && state.lastPanValue && *state.lastPanValue == value) {
    return;
  }
  addPan(track, automationState, tick, channel, value);
  state.lastPanValue = value;
}

void flushSimulatedPan(MidiTrack& track, RenderTrackState& state, u64 upToTick, u8 channel,
                       const PerformanceTempoMap& tempos) {
  flushLfo(state.panLfo, upToTick, tempos, [&](u64 tick, double value) {
    state.simulatedPanOffset = state.panLfo.depth * value;
    addCombinedPan(track, state, tick, channel);
  });
}

void setSimulatedPanDepth(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, double depth) {
  state.panLfo.depth = std::max(0.0, depth);
  if (state.panLfo.depth <= 0.0 && state.simulatedPanOffset != 0.0) {
    state.simulatedPanOffset = 0.0;
    addCombinedPan(track, state, tick, channel);
  }
}

void restartSimulatedPanForNote(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel) {
  if (!state.panLfo.configured) {
    return;
  }
  restartLfo(state.panLfo, tick);
  if (state.simulatedPanOffset != 0.0) {
    state.simulatedPanOffset = 0.0;
    addCombinedPan(track, state, tick, channel);
  }
}

bool shouldRestartSimulatedPanForNote(const PerformanceEvent& event, const RenderTrackState& state) {
  const auto* note = std::get_if<NotePerformanceEvent>(&event);
  return note != nullptr && !note->extendsPrevious && state.panLfo.configured;
}

void addMidiEvent(MidiTrack& track, RenderTrackState& state, const PerformanceEvent& event, u8 channel,
                  u32 sourceTrackNumber,
                  std::span<const GlobalTransposeChange> globalTransposes, const PerformanceTempoMap& globalTempos,
                  const MidiExportOptions& options, ModulationConversionPolicy modulationConversion,
                  std::span<const InstrumentSetAsset* const> instrumentSets,
                  const SequenceModulationProfile* modulationProfile, MidiControllerState* automationState) {
  std::visit(
      [&](const auto& typedEvent) {
        using TypedEvent = std::decay_t<decltype(typedEvent)>;
        if constexpr (std::is_same_v<TypedEvent, NotePerformanceEvent>) {
          const u8 key = midiKey(typedEvent.key + globalTransposeAt(globalTransposes, typedEvent.header.tick));
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            if (shouldRestartSimulatedVibratoForNote(event, state)) {
              restartSimulatedVibratoForNote(track, state, typedEvent.header.tick, channel);
            }
            if (shouldRestartSimulatedTremoloForNote(event, state)) {
              restartSimulatedTremoloForNote(track, state, typedEvent.header.tick, channel, options,
                                             modulationConversion);
            }
          }
          if (extendPreviousNote(track, state, typedEvent, channel)) {
            return;
          }
          restartSimulatedPanForNote(track, state, typedEvent.header.tick, channel);
          state.lastNoteIndex = track.events.size();
          track.events.push_back(NoteDuration{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .key = key,
              .velocity = midiVelocity(typedEvent.linearVelocity),
              .duration = typedEvent.durationTicks,
          });
        } else if constexpr (std::is_same_v<TypedEvent, TempoPerformanceEvent>) {
          // Tempo is song-wide. Effective changes are written once on the
          // first MIDI track after all source tracks have been lowered.
        } else if constexpr (std::is_same_v<TypedEvent, TimeSignaturePerformanceEvent>) {
          // Standard MIDI treats time signatures as global metadata. They are collected
          // once and written to the first MIDI track by renderMidiSequence.
        } else if constexpr (std::is_same_v<TypedEvent, InstrumentPerformanceEvent>) {
          const auto selection = instrumentSelection(typedEvent, instrumentSets);
          if (selection.address.bank != 0 || selection.forceBankSelect) {
            track.events.push_back(BankSelect{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .bank = midiBank(selection, options),
                .writeLsb = writeBankSelectLsb(options),
            });
          }
          track.events.push_back(ProgramChange{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .program = data7(selection.address.program),
          });
        } else if constexpr (std::is_same_v<TypedEvent, LevelPerformanceEvent>) {
          addVolume(track, automationState, typedEvent.header.tick, channel, typedEvent.linearGain,
                    typedEvent.precisionHint, options, typedEvent.sourceQuantization);
        } else if constexpr (std::is_same_v<TypedEvent, ExpressionPerformanceEvent>) {
          state.sourceExpressionGain = typedEvent.linearGain;
          state.sourceExpressionPrecisionHint = typedEvent.precisionHint;
          state.sourceExpressionQuantization = typedEvent.sourceQuantization;
          addCombinedExpression(track, state, typedEvent.header.tick, channel, options, modulationConversion,
                                automationState);
        } else if constexpr (std::is_same_v<TypedEvent, PanPerformanceEvent>) {
          const LoweredStereoBalance lowered = lowerPositionalPan(typedEvent.law, typedEvent.stereoPosition);
          state.sourcePanPosition = (static_cast<double>(lowered.pan) / 63.5) - 1.0;
          addCombinedPan(track, state, typedEvent.header.tick, channel, automationState, automationState == nullptr);
          const double previousPanExpressionGain = state.panExpressionGain;
          state.panExpressionGain = lowered.expressionGain * (typedEvent.hasLinearGain ? typedEvent.linearGain : 1.0);
          if (state.panExpressionGain != previousPanExpressionGain) {
            addCombinedExpression(track, state, typedEvent.header.tick, channel, options, modulationConversion,
                                  automationState);
          }
        } else if constexpr (std::is_same_v<TypedEvent, StereoBalancePerformanceEvent>) {
          const LoweredStereoBalance lowered = lowerStereoBalance(typedEvent.leftGain, typedEvent.rightGain);
          state.sourcePanPosition = (static_cast<double>(lowered.pan) / 63.5) - 1.0;
          addCombinedPan(track, state, typedEvent.header.tick, channel, automationState, automationState == nullptr);
          state.panExpressionGain = lowered.expressionGain;
          addCombinedExpression(track, state, typedEvent.header.tick, channel, options, modulationConversion,
                                automationState);
        } else if constexpr (std::is_same_v<TypedEvent, MasterLevelPerformanceEvent>) {
          track.events.push_back(MasterVolume{
              .tick = typedEvent.header.tick,
              .value = LevelScale::midi14FromLinear(typedEvent.linearGain),
          });
        } else if constexpr (std::is_same_v<TypedEvent, ReverbPerformanceEvent>) {
          const bool enabled = !typedEvent.voiceMask ||
                               (sourceTrackNumber < 8 && (*typedEvent.voiceMask & (1u << sourceTrackNumber)) != 0);
          track.events.push_back(Reverb{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = midiNormalized7(enabled ? typedEvent.send : 0.0),
          });
        } else if constexpr (std::is_same_v<TypedEvent, MonoModePerformanceEvent>) {
          track.events.push_back(MonoMode{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .channels = typedEvent.channels,
          });
        } else if constexpr (std::is_same_v<TypedEvent, TuningPerformanceEvent>) {
          track.events.push_back(FineTune{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .cents = typedEvent.cents,
          });
        } else if constexpr (std::is_same_v<TypedEvent, GlobalTransposePerformanceEvent>) {
          // Global transpose changes how later notes and portamento controls are written. It does not
          // become a MIDI event itself.
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendPerformanceEvent>) {
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            state.sourcePitchBendSemitones = typedEvent.semitones;
            addCombinedPitchBend(track, state, typedEvent.header.tick, channel, false);
          } else {
            addPitchBend(track, state, typedEvent.header.tick, channel,
                         midiPitchBend(typedEvent.semitones, state.pitchBendRangeCents));
          }
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendRangePerformanceEvent>) {
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            state.sourcePitchBendRangeCents = std::max<u16>(200, typedEvent.cents);
            ensurePitchBendRange(track, state, typedEvent.header.tick, channel,
                                 std::max(state.sourcePitchBendRangeCents, requiredPitchBendRangeCents(state)));
          } else {
            ensurePitchBendRange(track, state, typedEvent.header.tick, channel, typedEvent.cents);
          }
        } else if constexpr (std::is_same_v<TypedEvent, VibratoDelayPerformanceEvent>) {
          setLfoDelay(state.vibrato, typedEvent.header.tick, typedEvent.delayTicks, typedEvent.milliseconds,
                      typedEvent.tempoRelative);
          if (modulationConversion != ModulationConversionPolicy::SequenceEventSimulation) {
            track.events.push_back(VibratoDelay{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .ticks = vibratoDelayControllerValue(typedEvent, modulationProfile),
            });
          }
        } else if constexpr (std::is_same_v<TypedEvent, TremoloDelayPerformanceEvent>) {
          const auto fallback = typedEvent.milliseconds ? LfoInitialPhaseFallback::Zero
                                                        : LfoInitialPhaseFallback::UnipolarTremoloNominalGain;
          setLfoDelay(state.tremolo, typedEvent.header.tick, typedEvent.delayTicks, typedEvent.milliseconds,
                      typedEvent.tempoRelative, fallback);
          if (modulationConversion != ModulationConversionPolicy::SequenceEventSimulation) {
            track.events.push_back(TremoloDelay{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .ticks = tremoloDelayControllerValue(typedEvent, modulationProfile),
            });
          }
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoPerformanceEvent>) {
          track.events.push_back(PortamentoTime14{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = data14(typedEvent.timeMilliseconds),
          });
          if (typedEvent.previousKey) {
            const double previousKey =
                *typedEvent.previousKey + globalTransposeAt(globalTransposes, typedEvent.header.tick);
            track.events.push_back(PortamentoControl{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .key = midiKey(previousKey),
            });
          }
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoEnablePerformanceEvent>) {
          track.events.push_back(PortamentoEnable{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .enabled = typedEvent.enabled,
          });
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoTimePerformanceEvent>) {
          track.events.push_back(PortamentoTime{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = data7(typedEvent.timeMilliseconds),
          });
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoControlPerformanceEvent>) {
          const double previousKey =
              typedEvent.previousKey + globalTransposeAt(globalTransposes, typedEvent.header.tick);
          track.events.push_back(PortamentoControl{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .key = midiKey(previousKey),
          });
        } else if constexpr (std::is_same_v<TypedEvent, LegatoPedalPerformanceEvent>) {
          track.events.push_back(LegatoPedal{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .enabled = typedEvent.enabled,
          });
        } else if constexpr (std::is_same_v<TypedEvent, ModulationPerformanceEvent>) {
          const double normalizedAmount = modulationControllerAmount(typedEvent, modulationProfile);
          const u8 value = midiNormalized7(normalizedAmount);
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            switch (typedEvent.target) {
              case ModulationPerformanceTarget::VibratoDepth: {
                configureLfo(state.vibrato, typedEvent.header.tick, typedEvent);
                setSimulatedVibratoDepth(
                    track, state, typedEvent.header.tick, channel,
                    typedEvent.pitchDepthSemitones.value_or(std::clamp(typedEvent.amount, 0.0, 1.0) * 2.0));
                break;
              }
              case ModulationPerformanceTarget::TremoloDepth: {
                const bool physicalDecibels = typedEvent.volumeDepthDecibels.has_value();
                const auto fallback = !typedEvent.waveform && !physicalDecibels
                                          ? LfoInitialPhaseFallback::UnipolarTremoloNominalGain
                                          : LfoInitialPhaseFallback::Zero;
                configureLfo(state.tremolo, typedEvent.header.tick, typedEvent, fallback);
                setSimulatedTremoloDepth(
                    track, state, typedEvent.header.tick, channel,
                    physicalDecibels ? *typedEvent.volumeDepthDecibels : std::clamp(typedEvent.amount, 0.0, 1.0) * 0.5,
                    physicalDecibels, typedEvent.tremoloGainMode, options, modulationConversion);
                break;
              }
              case ModulationPerformanceTarget::VibratoRate:
                configureLfo(state.vibrato, typedEvent.header.tick, typedEvent);
                if (state.lastPitchBendValue) {
                  addCombinedPitchBend(track, state, typedEvent.header.tick, channel, false);
                }
                break;
              case ModulationPerformanceTarget::TremoloRate:
                configureLfo(state.tremolo, typedEvent.header.tick, typedEvent,
                             typedEvent.waveform ? LfoInitialPhaseFallback::Zero
                                                 : LfoInitialPhaseFallback::UnipolarTremoloNominalGain);
                break;
              case ModulationPerformanceTarget::PanDepth:
                configureLfo(state.panLfo, typedEvent.header.tick, typedEvent);
                setSimulatedPanDepth(track, state, typedEvent.header.tick, channel,
                                     typedEvent.panDepth.value_or(normalizedAmount));
                break;
              case ModulationPerformanceTarget::PanRate:
                configureLfo(state.panLfo, typedEvent.header.tick, typedEvent);
                break;
            }
            return;
          }
          if (typedEvent.target == ModulationPerformanceTarget::PanDepth ||
              typedEvent.target == ModulationPerformanceTarget::PanRate) {
            configureLfo(state.panLfo, typedEvent.header.tick, typedEvent);
            if (typedEvent.target == ModulationPerformanceTarget::PanDepth) {
              setSimulatedPanDepth(track, state, typedEvent.header.tick, channel,
                                   typedEvent.panDepth.value_or(normalizedAmount));
            }
            return;
          }
          switch (typedEvent.target) {
            case ModulationPerformanceTarget::VibratoDepth:
              track.events.push_back(VibratoDepth{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
              });
              break;
            case ModulationPerformanceTarget::VibratoRate:
              track.events.push_back(VibratoFrequency{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
              });
              break;
            case ModulationPerformanceTarget::TremoloDepth:
              track.events.push_back(TremoloDepth{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
              });
              break;
            case ModulationPerformanceTarget::TremoloRate:
              track.events.push_back(TremoloFrequency{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
              });
              break;
            case ModulationPerformanceTarget::PanDepth:
            case ModulationPerformanceTarget::PanRate:
              break;
          }
        } else if constexpr (std::is_same_v<TypedEvent, MarkerPerformanceEvent>) {
          track.events.push_back(Marker{
              .tick = typedEvent.header.tick,
              .text = typedEvent.text,
          });
        }
      },
      event);
}

}  // namespace

MidiSequence renderMidiSequence(const PerformanceSequence& performance, MidiExportOptions options,
                                ModulationConversionPolicy modulationConversion,
                                std::span<const InstrumentSetAsset* const> instrumentSets,
                                const SequenceModulationProfile* modulationProfile) {
  std::optional<SequenceModulationProfile> derivedModulationProfile;
  if (modulationProfile == nullptr &&
      std::ranges::any_of(performance.tracks, &PerformanceTrack::hasPhysicalModulation)) {
    derivedModulationProfile = analyzeSequenceModulation(performance);
    modulationProfile = &*derivedModulationProfile;
  }

  const PerformanceTempoMap globalTempos{performance};
  const std::vector<PerformanceTempoMap::Point> globalTempoPoints = globalTempos.points();
  std::vector<bool> renderedTempoPoints(globalTempoPoints.size(), false);
  const PerformanceSequence loweredPerformance = lowerMidiPerformanceAutomation(performance, options, globalTempos);
  MidiSequence sequence{
      .timebase = loweredPerformance.timebase,
      .diagnostics = loweredPerformance.diagnostics,
  };
  sequence.tracks.reserve(loweredPerformance.tracks.size());
  const PerformanceTimelines timelines = buildPerformanceTimelines(loweredPerformance);
  const std::vector<GlobalTransposeChange> globalTransposes = collectGlobalTransposeChanges(timelines);
  const std::vector<TimeSignature> globalTimeSignatures = collectGlobalTimeSignatures(timelines);

  for (size_t trackIndex = 0; trackIndex < loweredPerformance.tracks.size(); ++trackIndex) {
    const auto& performanceTrack = loweredPerformance.tracks[trackIndex];
    MidiTrack midiTrack{
        .name = "Track " + std::to_string(performanceTrack.sourceTrackNumber),
    };
    RenderTrackState renderState;
    std::unordered_map<PerformanceAutomationId, MidiControllerState> automationControllerStates;
    const auto assignment = midiChannelAssignment(trackIndex, options);
    if (assignment.port > 255) {
      sequence.diagnostics.push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "MIDI port number exceeded the Standard MIDI File port meta-event range",
      });
    }
    if (options.writePortMetaEvents) {
      midiTrack.events.push_back(MidiPort{
          .tick = 0,
          .port = midiPortByte(assignment.port),
      });
    }
    for (const auto* event : timelines[trackIndex]) {
      u64 flushTick = performanceEventHeader(*event).tick;
      if (((modulationConversion == ModulationConversionPolicy::SequenceEventSimulation &&
            (shouldRestartSimulatedVibratoForNote(*event, renderState) ||
             shouldRestartSimulatedTremoloForNote(*event, renderState))) ||
           shouldRestartSimulatedPanForNote(*event, renderState)) &&
          flushTick != 0) {
        --flushTick;
      }
      if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
        flushSimulatedVibrato(midiTrack, renderState, flushTick, assignment.channel, globalTempos);
        flushSimulatedTremolo(midiTrack, renderState, flushTick, assignment.channel, globalTempos, options,
                              modulationConversion);
      }
      flushSimulatedPan(midiTrack, renderState, flushTick, assignment.channel, globalTempos);
      const auto& header = performanceEventHeader(*event);
      if (trackIndex == 0) {
        if (const auto* tempo = std::get_if<TempoPerformanceEvent>(event);
            tempo != nullptr && globalTempos.contains(*tempo)) {
          midiTrack.events.push_back(Tempo{
              .tick = tempo->header.tick,
              .microsecondsPerQuarter = tempo->microsecondsPerQuarter,
          });
          for (size_t index = 0; index < globalTempoPoints.size(); ++index) {
            if (!renderedTempoPoints[index] && globalTempoPoints[index].tick == tempo->header.tick &&
                globalTempoPoints[index].microsecondsPerQuarter == tempo->microsecondsPerQuarter) {
              renderedTempoPoints[index] = true;
              break;
            }
          }
          continue;
        }
      }
      MidiControllerState* automationState =
          header.automation ? &automationControllerStates[*header.automation] : nullptr;
      addMidiEvent(midiTrack, renderState, *event, assignment.channel, performanceTrack.sourceTrackNumber,
                   globalTransposes, globalTempos, options, modulationConversion, instrumentSets, modulationProfile,
                   automationState);
    }
    u64 endTick = performanceTrack.endTick;
    if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
      flushSimulatedVibrato(midiTrack, renderState, endTick, assignment.channel, globalTempos);
      flushSimulatedTremolo(midiTrack, renderState, endTick, assignment.channel, globalTempos, options,
                            modulationConversion);
    }
    flushSimulatedPan(midiTrack, renderState, endTick, assignment.channel, globalTempos);
    if (trackIndex == 0) {
      for (size_t index = 0; index < globalTempoPoints.size(); ++index) {
        if (renderedTempoPoints[index]) {
          continue;
        }
        const auto& tempo = globalTempoPoints[index];
        midiTrack.events.push_back(Tempo{
            .tick = tempo.tick,
            .microsecondsPerQuarter = tempo.microsecondsPerQuarter,
        });
        endTick = std::max(endTick, tempo.tick);
      }
      midiTrack.events.insert(midiTrack.events.end(), globalTimeSignatures.begin(), globalTimeSignatures.end());
      for (const auto& timeSignature : globalTimeSignatures) {
        endTick = std::max(endTick, timeSignature.tick);
      }
    }
    midiTrack.events.push_back(EndOfTrack{
        .tick = endTick,
    });
    sequence.tracks.push_back(std::move(midiTrack));
  }

  return sequence;
}

}  // namespace vgmtrans::core

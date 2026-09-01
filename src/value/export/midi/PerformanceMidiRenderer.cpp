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
#include <tuple>
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

void addController(MidiTrack& track, u64 tick, u8 channel, MidiController controller, s32 value, int priority = 20,
                   std::optional<double> normalizedAmount = std::nullopt) {
  track.events.push_back(midi::controller(tick, channel, controller, value, priority, normalizedAmount));
}

void addPitchBendRange(MidiTrack& track, u64 tick, u8 channel, u16 cents) {
  const u8 semitones = static_cast<u8>(std::min<u16>(cents / 100, 127));
  const u8 fineCents = static_cast<u8>(std::min<u16>(cents % 100, 127));
  midi::appendRpn(track, tick, channel, 0, 0, static_cast<u16>((semitones << 7) | fineCents));
}

void addFineTune(MidiTrack& track, u64 tick, u8 channel, double cents) {
  const double semitones = std::clamp(cents / 100.0, -1.0, 1.0);
  const s32 value = std::min(static_cast<int>(std::lround(8192 * semitones)), 8191) + 8192;
  midi::appendRpn(track, tick, channel, 0, 1, static_cast<u16>(value), 8);
}

void addCoarseTune(MidiTrack& track, u64 tick, u8 channel, s8 semitones) {
  const s32 value = std::clamp<s32>((semitones + 64) << 7, 0, 16383);
  midi::appendRpn(track, tick, channel, 0, 2, static_cast<u16>(value), 8);
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
      .gain = midiGain == 0.0 ? 0.0 : (sourceLeft + sourceRight) / midiGain,
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
                                                         std::optional<ValueQuantization> quantization = std::nullopt) {
  if (requested != MidiLevelResolution::Auto) {
    return requested;
  }
  if (quantization && quantization->levels > 128) {
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

struct MidiControllerState {
  std::optional<MidiLevelState> volume;
  std::optional<MidiLevelState> expression;
  std::optional<u8> pan;
};

void addLevelController(MidiTrack& track, std::optional<MidiLevelState>* state, u64 tick, u8 channel,
                        MidiController controller, double linearGain, MidiLevelResolution requestedResolution,
                        std::optional<ValueQuantization> quantization = std::nullopt) {
  const MidiLevelResolution resolution = resolveLevelResolution(requestedResolution, quantization);
  const u16 value = resolution == MidiLevelResolution::FourteenBit ? LevelScale::midi14FromLinear(linearGain)
                                                                   : LevelScale::midi7FromLinear(linearGain);
  const MidiLevelState nextState{.resolution = resolution, .value = value};
  if (state != nullptr && state->has_value() && **state == nextState) {
    return;
  }

  if (resolution == MidiLevelResolution::FourteenBit) {
    midi::appendController14(track, tick, channel, controller, value);
  } else {
    addController(track, tick, channel, controller, static_cast<u8>(value));
  }
  if (state != nullptr) {
    *state = nextState;
  }
}

void addVolume(MidiTrack& track, MidiControllerState* state, u64 tick, u8 channel, double linearGain,
               const MidiExportOptions& options, std::optional<ValueQuantization> quantization = std::nullopt) {
  addLevelController(track, state != nullptr ? &state->volume : nullptr, tick, channel, MidiController::ChannelVolume,
                     linearGain, options.volumeResolution, quantization);
}

void addExpression(MidiTrack& track, MidiControllerState* state, u64 tick, u8 channel, double linearGain,
                   const MidiExportOptions& options, std::optional<ValueQuantization> quantization = std::nullopt) {
  addLevelController(track, state != nullptr ? &state->expression : nullptr, tick, channel, MidiController::Expression,
                     linearGain, options.expressionResolution, quantization);
}

void addPan(MidiTrack& track, MidiControllerState* state, u64 tick, u8 channel, u8 value) {
  if (state != nullptr && state->pan && *state->pan == value) {
    return;
  }
  addController(track, tick, channel, MidiController::Pan, value);
  if (state != nullptr) {
    state->pan = value;
  }
}

struct MidiInstrumentSelection {
  InstrumentAddress address;
  bool forceBankSelect = false;
  std::optional<u16> pitchBendRangeCents;
};

[[nodiscard]] MidiInstrumentSelection instrumentSelection(const InstrumentPerformanceEvent& event,
                                                          std::span<const SoundBankAsset* const> soundBanks) {
  const Instrument* instrument = findPerformanceInstrument(event, soundBanks);
  if (!event.sourceInstrument) {
    return MidiInstrumentSelection{
        .address = resolveInstrumentAddress(InstrumentAddress{.bank = event.bank, .program = event.program}, {}),
        .forceBankSelect = event.forceBankSelect,
        .pitchBendRangeCents = instrument != nullptr ? instrument->pitchBendRangeCents : std::nullopt,
    };
  }

  if (instrument != nullptr) {
    return MidiInstrumentSelection{
        .address = resolveInstrumentAddress(instrument->explicitAddress, instrument->identity),
        .forceBankSelect = true,
        .pitchBendRangeCents = instrument->pitchBendRangeCents,
    };
  }

  // A sequential key is a deterministic fallback for incomplete collections.
  // This is an export policy, not a bank convention imposed on format code.
  return MidiInstrumentSelection{
      .address = resolveInstrumentAddress({}, event.sourceInstrument),
      .forceBankSelect = true,
  };
}

struct SimulatedLfoDelay {
  u32 ticks = 0;
  std::optional<double> milliseconds;
  bool tempoRelative = false;
};

struct SimulatedLfoState {
  double depth = 0.0;
  double frequencyHz = 0.0;
  std::optional<double> cyclesPerTick;
  SimulatedLfoDelay delay;
  std::optional<SimulatedLfoDelay> noteRestartDelay;
  u32 delayCounterTicks = 0;
  double delayCounterMilliseconds = 0.0;
  u64 cursorTick = 0;
  double phaseCycles = 0.0;
  std::optional<LfoShape> shape;
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
  bool configured = false;
  bool started = false;
  bool producedSample = false;
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

struct RenderTrackState {
  std::optional<size_t> lastNoteIndex;
  // MIDI starts in bank/program zero. Keep the actual emitted state so a
  // generated selection can explicitly return to bank zero.
  u16 midiBank = 0;
  u8 midiProgram = 0;
  u16 pitchBendRangeCents = 200;
  std::optional<u16> lastPitchBendRangeCents;
  PerformancePitchBendContext pitchBendContext;
  // Slides may replace the sequence range, but they must not reduce the range
  // required by the selected instrument.
  std::optional<u16> voicePitchBendRangeCents;
  double tuningBendSemitones = 0.0;
  PitchBendLayers pitchBendLayers;
  std::map<u32, SimulatedPitchLfoState> pitchLfos;
  std::optional<s16> lastPitchBendValue;
  std::optional<size_t> lastPitchBendIndex;
  double sourceLevelGain = 1.0;
  std::optional<ValueQuantization> sourceLevelQuantization;
  double panLevelGain = 1.0;
  double levelHeadroom = 1.0;
  bool levelEmitted = false;
  double sourceExpressionGain = 1.0;
  std::optional<ValueQuantization> sourceExpressionQuantization;
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
};

struct GlobalTransposeChange {
  u64 tick = 0;
  s32 semitones = 0;
  size_t sequence = 0;
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
    std::ranges::stable_sort(timeline, [](const PerformanceEvent* lhs, const PerformanceEvent* rhs) {
      const auto& left = performanceEventHeader(*lhs);
      const auto& right = performanceEventHeader(*rhs);
      return std::tie(left.tick, left.sequence) < std::tie(right.tick, right.sequence);
    });
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
        sourcePanLinearGain = pan->hasLinearGain ? pan->linearGain : 1.0;
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
           std::tie(voices[nextVoice].startTick, voices[nextVoice].startSequence) <=
               std::tie(header.tick, header.sequence)) {
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

[[nodiscard]] std::vector<MidiEvent> collectGlobalTimeSignatures(const PerformanceTimelines& timelines) {
  std::vector<MidiEvent> timeSignatures;
  for (const auto& timeline : timelines) {
    for (const auto* event : timeline) {
      const auto* timeSignature = std::get_if<TimeSignaturePerformanceEvent>(event);
      if (timeSignature == nullptr) {
        continue;
      }
      timeSignatures.push_back(timeSignatureEvent(timeSignature->header.tick, timeSignature->numerator,
                                                  timeSignature->denominator, timeSignature->clocksPerMetronomeClick));
    }
  }
  std::ranges::stable_sort(timeSignatures,
                           [](const MidiEvent& lhs, const MidiEvent& rhs) { return lhs.tick < rhs.tick; });
  return timeSignatures;
}

bool extendPreviousNote(MidiTrack& track, RenderTrackState& state, const NotePerformanceEvent& note, u8 channel) {
  if (!note.extendsPrevious || !state.lastNoteIndex || *state.lastNoteIndex >= track.events.size()) {
    return false;
  }

  MidiEvent& previousEvent = track.events[*state.lastNoteIndex];
  auto* previous = std::get_if<NoteDuration>(&previousEvent.payload);
  if (previous == nullptr || previous->channel != channel) {
    return false;
  }

  const u64 previousEnd = previousEvent.tick + previous->duration;
  const u64 extensionEnd = note.header.tick + note.durationTicks;
  if (extensionEnd > previousEnd) {
    previous->duration = static_cast<u32>(extensionEnd - previousEvent.tick);
  }
  return true;
}

[[nodiscard]] double layeredPitchBendSemitones(const RenderTrackState& state) {
  return state.pitchBendLayers.semitones(state.pitchBendContext);
}

[[nodiscard]] SimulatedPitchLfoState& pitchLfo(RenderTrackState& state, PitchBendLayerId layer) {
  return state.pitchLfos[layer.value];
}

[[nodiscard]] double simulatedPitchLfoSemitones(const RenderTrackState& state) {
  double semitones = 0.0;
  for (const auto& entry : state.pitchLfos) {
    semitones += entry.second.semitones;
  }
  return semitones;
}

[[nodiscard]] u16 requiredPitchBendRangeCents(const RenderTrackState& state) {
  double voicePitch = state.tuningBendSemitones + layeredPitchBendSemitones(state);
  double maximumLfoExcursion = 0.0;
  for (const auto& entry : state.pitchLfos) {
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

[[nodiscard]] u16 effectivePitchBendRangeCents(const RenderTrackState& state,
                                               ModulationConversionPolicy modulationConversion) {
  const u16 tuningRangeCents =
      state.tuningBendSemitones == 0.0
          ? 0
          : static_cast<u16>(
                std::clamp(std::ceil(std::abs(state.tuningBendSemitones + layeredPitchBendSemitones(state)) * 100.0), 0.0,
                           static_cast<double>(std::numeric_limits<u16>::max())));
  const u16 range = std::max({state.voicePitchBendRangeCents.value_or(state.pitchBendContext.sourceRangeCents()),
                              state.pitchBendContext.instrumentRangeCents().value_or(0), tuningRangeCents});
  const bool simulatesPitchLfo =
      modulationConversion == ModulationConversionPolicy::SequenceEventSimulation ||
      std::ranges::any_of(state.pitchLfos, [](const auto& entry) {
        return entry.first != kPrimaryPitchBendLayer.value && entry.second.oscillator.configured;
      });
  return simulatesPitchLfo
             ? std::max(range, requiredPitchBendRangeCents(state))
             : range;
}

void ensurePitchBendRange(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, u16 cents) {
  const u16 range = wholeSemitonePitchBendRangeCents(cents);
  if (state.lastPitchBendRangeCents && *state.lastPitchBendRangeCents == range) {
    state.pitchBendRangeCents = range;
    return;
  }
  addPitchBendRange(track, tick, channel, range);
  state.pitchBendRangeCents = range;
  state.lastPitchBendRangeCents = range;
}

void addPitchBend(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, s16 value, bool force = false) {
  if (!force && state.lastPitchBendValue && *state.lastPitchBendValue == value) {
    return;
  }
  if (state.lastPitchBendIndex && *state.lastPitchBendIndex < track.events.size()) {
    MidiEvent& previous = track.events[*state.lastPitchBendIndex];
    auto* message = std::get_if<MidiChannelMessage>(&previous.payload);
    if (previous.tick == tick && message != nullptr && message->kind == MidiChannelMessageKind::PitchBend &&
        message->channel == channel) {
      message->value = value;
      state.lastPitchBendValue = value;
      return;
    }
  }
  state.lastPitchBendIndex = track.events.size();
  track.events.push_back(midi::pitchBend(tick, channel, value));
  state.lastPitchBendValue = value;
}

[[nodiscard]] double currentPitchBendSemitones(const RenderTrackState& state,
                                               ModulationConversionPolicy modulationConversion) {
  static_cast<void>(modulationConversion);
  return state.tuningBendSemitones + layeredPitchBendSemitones(state) + simulatedPitchLfoSemitones(state);
}

void refreshPitchBendRange(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, u16 cents,
                           ModulationConversionPolicy modulationConversion) {
  ensurePitchBendRange(track, state, tick, channel, cents);
  if (state.lastPitchBendValue) {
    // A source or instrument range can reinterpret a normalized layer even
    // when whole-semitone MIDI sensitivity remains unchanged.
    addPitchBend(track, state, tick, channel,
                 midiPitchBend(currentPitchBendSemitones(state, modulationConversion), state.pitchBendRangeCents));
  }
}

void applyInstrumentPitchBendRange(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                                   std::optional<u16> cents, ModulationConversionPolicy modulationConversion) {
  const u16 previousRange = effectivePitchBendRangeCents(state, modulationConversion);
  state.pitchBendContext.setInstrumentRangeCents(cents);
  const u16 range = effectivePitchBendRangeCents(state, modulationConversion);
  if (!state.lastPitchBendValue &&
      wholeSemitonePitchBendRangeCents(range) == wholeSemitonePitchBendRangeCents(previousRange)) {
    return;
  }
  refreshPitchBendRange(track, state, tick, channel, range, modulationConversion);
}

void applyInstrumentSelection(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                              const MidiInstrumentSelection& selection, const MidiExportOptions& options,
                              ModulationConversionPolicy modulationConversion, bool forceProgramChange) {
  const u16 bank = static_cast<u16>(selection.address.bank & 0x3fff);
  const u16 emittedBank =
      options.bankSelectStyle == MidiBankSelectStyle::MsbOnly ? static_cast<u16>(bank & 0x7f) : bank;
  const bool bankChanged = emittedBank != state.midiBank;
  if (bankChanged || selection.forceBankSelect) {
    track.events.push_back(midi::bankSelect(tick, channel, bank, writeBankSelectLsb(options)));
    state.midiBank = emittedBank;
  }
  const u8 program = data7(selection.address.program);
  if (forceProgramChange || bankChanged || program != state.midiProgram) {
    state.midiProgram = program;
    track.events.push_back(midi::programChange(tick, channel, program));
  }
  applyInstrumentPitchBendRange(track, state, tick, channel, selection.pitchBendRangeCents, modulationConversion);
}

void applyVoicePitchBendRangeChange(MidiTrack& track, RenderTrackState& state, const VoicePitchBendRangeChange& change,
                                    u8 channel, ModulationConversionPolicy modulationConversion) {
  state.pitchBendContext.setSourceRangeCents(change.sourceCents);
  state.voicePitchBendRangeCents = change.voiceCents;
  refreshPitchBendRange(track, state, change.tick, channel, effectivePitchBendRangeCents(state, modulationConversion),
                        modulationConversion);
}

void addCurrentPitchBend(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                         ModulationConversionPolicy modulationConversion, bool force = true) {
  ensurePitchBendRange(track, state, tick, channel, effectivePitchBendRangeCents(state, modulationConversion));
  const s16 value = midiPitchBend(currentPitchBendSemitones(state, modulationConversion), state.pitchBendRangeCents);
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

void applyLfoDelayUpdate(SimulatedLfoState& lfo, SimulatedLfoDelay delay, LfoDelayUpdateMode mode) {
  lfo.noteRestartDelay = delay;
  if (mode == LfoDelayUpdateMode::CurrentAndFutureNotes) {
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
    lfo.shape = context.shape;
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
  if (context.delayTicks || context.delayMilliseconds) {
    SimulatedLfoDelay delay{
        .ticks = context.delayTicks.value_or(0),
        .milliseconds =
            context.delayMilliseconds ? std::optional{std::max(0.0, *context.delayMilliseconds)} : std::nullopt,
        .tempoRelative = context.delayIsTempoRelative,
    };
    applyLfoDelayUpdate(lfo, std::move(delay), context.delayUpdateMode);
  }
  lfo.phaseRunsAtZeroDepth = context.phaseRunsAtZeroDepth;
  lfo.delayRunsWhileInactive = context.delayRunsWhileInactive;
  lfo.restartsOnNote = context.restartsOnNote;
  lfo.configured = true;
  applyLfoRestart(lfo, tick, lfo.started ? context.restartMode : LfoRestartMode::PhaseAndDelay, fallback);
}

void restartNoteLfo(SimulatedLfoState& lfo, u64 tick,
                    LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  if (lfo.noteRestartDelay) {
    lfo.delay = *lfo.noteRestartDelay;
  }
  applyLfoRestart(lfo, tick, LfoRestartMode::PhaseAndDelay, fallback);
  if (lfo.noteRestartInitialPhaseCycles) {
    lfo.phaseCycles = *lfo.noteRestartInitialPhaseCycles - std::floor(*lfo.noteRestartInitialPhaseCycles);
  }
}

void setLfoDelay(SimulatedLfoState& lfo, u64 tick, u32 delayTicks, std::optional<double> delayMilliseconds,
                 bool tempoRelative, LfoDelayUpdateMode updateMode,
                 LfoInitialPhaseFallback fallback = LfoInitialPhaseFallback::Zero) {
  SimulatedLfoDelay delay{
      .ticks = delayTicks,
      .milliseconds = delayMilliseconds ? std::optional{std::max(0.0, *delayMilliseconds)} : std::nullopt,
      .tempoRelative = tempoRelative,
  };
  applyLfoDelayUpdate(lfo, std::move(delay), updateMode);
  lfo.configured = true;
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

void flushSimulatedVibrato(MidiTrack& track, RenderTrackState& state, u64 upToTick, u8 channel,
                           const PerformanceTempoMap& tempos, const NotePerformanceEvent* note = nullptr) {
  const bool restartsPitch =
      note != nullptr && note->restartsVibratoLfoPhase.value_or(!note->extendsPrevious && note->restartsLfoPhase);
  const auto layerEnd = [&](const SimulatedPitchLfoState& pitch) {
    return restartsPitch && pitch.oscillator.restartsOnNote && upToTick != 0 ? upToTick - 1 : upToTick;
  };
  while (true) {
    std::optional<u64> nextTick;
    for (const auto& entry : state.pitchLfos) {
      const auto& pitch = entry.second;
      if (pitch.oscillator.cursorTick < layerEnd(pitch)) {
        nextTick = std::min(nextTick.value_or(pitch.oscillator.cursorTick + 1), pitch.oscillator.cursorTick + 1);
      }
    }
    if (!nextTick) {
      break;
    }
    bool sampled = false;
    for (auto& entry : state.pitchLfos) {
      auto& pitch = entry.second;
      flushLfo(pitch.oscillator, std::min(*nextTick, layerEnd(pitch)), tempos, [&](u64, double value) {
        pitch.semitones = simulatedVibratoAtPhase(pitch.oscillator, value);
        sampled = true;
      });
    }
    if (sampled) {
      addCurrentPitchBend(track, state, *nextTick, channel, ModulationConversionPolicy::SequenceEventSimulation,
                          false);
    }
  }
}

void setSimulatedVibratoDepth(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, double semitones,
                              LfoZeroDepthBehavior zeroDepthBehavior, PitchBendLayerId layer) {
  auto& pitch = pitchLfo(state, layer);
  auto& lfo = pitch.oscillator;
  lfo.depth = std::max(0.0, semitones);
  lfo.outputHeldUntilNextNote = lfo.depth <= 0.0 && zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote;
  if (lfo.depth <= 0.0 && !lfo.outputHeldUntilNextNote) {
    pitch.semitones = 0.0;
    addCurrentPitchBend(track, state, tick, channel, ModulationConversionPolicy::SequenceEventSimulation, false);
  }
}

void sampleRestartedVibrato(MidiTrack& track, RenderTrackState& state, const ModulationPerformanceEvent& event,
                            u8 channel) {
  auto& pitch = pitchLfo(state, event.pitchLayer);
  auto& lfo = pitch.oscillator;
  const bool immediate = event.context.restartMode == LfoRestartMode::PhaseAndDelay && lfo.sampleImmediatelyOnNote &&
                         lfo.delay.ticks == 0 && lfo.delay.milliseconds.value_or(0.0) <= 0.0 && lfo.depth > 0.0 &&
                         lfo.cyclesPerTick.value_or(lfo.frequencyHz) > 0.0;
  if (!immediate) {
    return;
  }
  const double value = simulatedVibratoAtPhase(lfo, lfoValue(lfo));
  lfo.producedSample = true;
  if (value != pitch.semitones) {
    pitch.semitones = value;
    addCurrentPitchBend(track, state, event.header.tick, channel, ModulationConversionPolicy::SequenceEventSimulation,
                        false);
  }
}

void restartSimulatedVibratoForNote(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel) {
  bool changed = false;
  for (auto& entry : state.pitchLfos) {
    auto& pitch = entry.second;
    auto& lfo = pitch.oscillator;
    if (!lfo.configured || !lfo.restartsOnNote) {
      continue;
    }
    restartNoteLfo(lfo, tick);
    lfo.outputHeldUntilNextNote = false;
    const double previousSemitones = pitch.semitones;
    const bool startsImmediately = lfo.sampleImmediatelyOnNote && lfo.delay.ticks == 0 &&
                                   lfo.delay.milliseconds.value_or(0.0) <= 0.0 &&
                                   lfo.cyclesPerTick.value_or(lfo.frequencyHz) > 0.0 && lfo.depth > 0.0;
    pitch.semitones = startsImmediately ? simulatedVibratoAtPhase(lfo, lfoValue(lfo)) : 0.0;
    lfo.producedSample = startsImmediately;
    changed |= startsImmediately || previousSemitones != 0.0;
  }
  if (changed) {
    addCurrentPitchBend(track, state, tick, channel, ModulationConversionPolicy::SequenceEventSimulation, false);
  }
}

bool shouldRestartSimulatedVibratoForNote(const NotePerformanceEvent& note, const RenderTrackState& state) {
  if (!note.restartsVibratoLfoPhase.value_or(!note.extendsPrevious && note.restartsLfoPhase)) {
    return false;
  }
  return std::ranges::any_of(state.pitchLfos, [](const auto& entry) {
    return entry.second.oscillator.configured && entry.second.oscillator.restartsOnNote;
  });
}

[[nodiscard]] bool releaseHeldLfoOutput(SimulatedLfoState& lfo) {
  const bool wasHeld = lfo.outputHeldUntilNextNote;
  lfo.outputHeldUntilNextNote = false;
  return wasHeld;
}

void releaseHeldPitchLfoOutputs(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel) {
  bool changed = false;
  for (auto& entry : state.pitchLfos) {
    auto& pitch = entry.second;
    if (releaseHeldLfoOutput(pitch.oscillator) && pitch.semitones != 0.0) {
      pitch.semitones = 0.0;
      changed = true;
    }
  }
  if (changed) {
    addCurrentPitchBend(track, state, tick, channel, ModulationConversionPolicy::SequenceEventSimulation, false);
  }
}

// Pan-law conversion can require gain above unity. A sequence-wide headroom
// factor bounds that gain without changing the mix between tracks. Keep it on
// channel volume so source expression remains an independent control flow.
void addCombinedLevel(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, const MidiExportOptions& options,
                      MidiControllerState* automationState = nullptr) {
  addVolume(track, automationState, tick, channel, state.sourceLevelGain * state.panLevelGain * state.levelHeadroom,
            options, state.sourceLevelQuantization);
  state.levelEmitted = true;
}

// Source expression and simulated tremolo share the remaining MIDI expression
// controller. Pan conversion deliberately does not participate in this product.
void addCombinedExpression(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                           const MidiExportOptions& options, ModulationConversionPolicy modulationConversion,
                           MidiControllerState* automationState = nullptr) {
  const bool simulatingTremolo = modulationConversion == ModulationConversionPolicy::SequenceEventSimulation;
  addExpression(track, automationState, tick, channel, state.sourceExpressionGain * state.simulatedTremoloGain, options,
                simulatingTremolo ? std::nullopt : state.sourceExpressionQuantization);
}

[[nodiscard]] double simulatedTremoloGain(const RenderTrackState& state, double lfoValue) {
  const double depth = state.tremolo.depth * lfoDepthScale(state.tremolo);
  if (state.tremoloDepthUnit == RenderTrackState::TremoloDepthUnit::Decibels) {
    double gainDecibels = depth * lfoValue;
    if (state.tremoloGainMode == TremoloGainMode::NoBoost) {
      gainDecibels -= depth;
    }
    return std::pow(10.0, gainDecibels / 20.0);
  }
  if (state.tremoloDepthUnit == RenderTrackState::TremoloDepthUnit::LinearGain) {
    return std::max(0.0, 1.0 + depth * lfoValue);
  }

  const double normalizedLfo = (lfoValue + 1.0) / 2.0;
  return 1.0 - (depth * normalizedLfo);
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

  const double value = lfoValue(state.tremolo);
  return simulatedTremoloGain(state, value);
}

void setSimulatedTremoloDepth(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, double depth,
                              RenderTrackState::TremoloDepthUnit unit, TremoloGainMode gainMode,
                              LfoZeroDepthBehavior zeroDepthBehavior, const MidiExportOptions& options,
                              ModulationConversionPolicy modulationConversion) {
  state.tremolo.depth = std::max(0.0, depth);
  state.tremoloDepthUnit = unit;
  state.tremoloGainMode = gainMode;
  state.tremolo.outputHeldUntilNextNote =
      state.tremolo.depth <= 0.0 && zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote;
  if (state.tremolo.outputHeldUntilNextNote) {
    return;
  }
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

  const auto fallback =
      !state.tremolo.shape && state.tremoloDepthUnit == RenderTrackState::TremoloDepthUnit::LegacyUnipolar
          ? LfoInitialPhaseFallback::UnipolarTremoloNominalGain
          : LfoInitialPhaseFallback::Zero;
  restartNoteLfo(state.tremolo, tick, fallback);
  state.tremolo.outputHeldUntilNextNote = false;
  const bool delayed = state.tremolo.delay.ticks != 0 || state.tremolo.delay.milliseconds.value_or(0.0) > 0.0;
  const double gain = delayed ? 1.0 : tremoloGainAtCurrentPhase(state);
  state.tremolo.producedSample = !delayed && state.tremolo.sampleImmediatelyOnNote &&
                                 state.tremolo.cyclesPerTick.value_or(state.tremolo.frequencyHz) > 0.0 &&
                                 state.tremolo.depth > 0.0;
  if (gain != state.simulatedTremoloGain) {
    state.simulatedTremoloGain = gain;
    addCombinedExpression(track, state, tick, channel, options, modulationConversion);
  }
}

bool shouldRestartSimulatedTremoloForNote(const NotePerformanceEvent& note, const RenderTrackState& state) {
  return note.restartsTremoloLfoPhase.value_or(!note.extendsPrevious && note.restartsLfoPhase) &&
         state.tremolo.configured;
}

void addCombinedPan(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, const MidiExportOptions& options,
                    MidiControllerState* automationState = nullptr, bool force = false) {
  const double position = std::clamp(state.sourcePanPosition + state.simulatedPanOffset, -1.0, 1.0);
  const PanLaw law = state.panLfo.panLaw != PanLaw::Unspecified ? state.panLfo.panLaw : state.sourcePanLaw;
  u8 value = midiPan(position);
  double levelGain = state.panLevelGain;
  if (law != PanLaw::Unspecified) {
    const LoweredStereoBalance lowered = lowerPositionalPan(law, position);
    value = lowered.pan;
    levelGain = lowered.gain * state.sourcePanLinearGain;
  }
  if (automationState == nullptr && !force && state.lastPanValue && *state.lastPanValue == value) {
    if (levelGain == state.panLevelGain) {
      return;
    }
  } else {
    addPan(track, automationState, tick, channel, value);
    state.lastPanValue = value;
  }
  if (levelGain != state.panLevelGain) {
    state.panLevelGain = levelGain;
    addCombinedLevel(track, state, tick, channel, options, automationState);
  }
}

void flushSimulatedPan(MidiTrack& track, RenderTrackState& state, u64 upToTick, u8 channel,
                       const PerformanceTempoMap& tempos, const MidiExportOptions& options) {
  flushLfo(state.panLfo, upToTick, tempos, [&](u64 tick, double value) {
    state.simulatedPanOffset = state.panLfo.depth * value;
    addCombinedPan(track, state, tick, channel, options);
  });
}

void setSimulatedPanDepth(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, double depth,
                          const MidiExportOptions& options) {
  state.panLfo.depth = std::max(0.0, depth);
  if (state.panLfo.depth <= 0.0 && state.simulatedPanOffset != 0.0) {
    state.simulatedPanOffset = 0.0;
    addCombinedPan(track, state, tick, channel, options);
  }
}

void restartSimulatedPanForNote(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                                const MidiExportOptions& options) {
  if (!state.panLfo.configured) {
    return;
  }
  restartNoteLfo(state.panLfo, tick);
  if (state.simulatedPanOffset != 0.0) {
    state.simulatedPanOffset = 0.0;
    addCombinedPan(track, state, tick, channel, options);
  }
}

bool shouldRestartSimulatedPanForNote(const NotePerformanceEvent& note, const RenderTrackState& state) {
  return !note.extendsPrevious && note.restartsLfoPhase && state.panLfo.configured;
}

void addMidiEvent(MidiTrack& track, RenderTrackState& state, const PerformanceEvent& event, u8 channel,
                  u32 sourceTrackNumber, std::span<const GlobalTransposeChange> globalTransposes,
                  const PerformanceTempoMap& globalTempos, const MidiExportOptions& options,
                  ModulationConversionPolicy modulationConversion, std::span<const SoundBankAsset* const> soundBanks,
                  const SequenceModulationProfile* modulationProfile, MidiControllerState* automationState) {
  std::visit(
      [&](const auto& typedEvent) {
        using TypedEvent = std::decay_t<decltype(typedEvent)>;
        if constexpr (std::is_same_v<TypedEvent, NotePerformanceEvent>) {
          if (!typedEvent.extendsPrevious && typedEvent.instrumentAddress) {
            const auto address = *typedEvent.instrumentAddress;
            applyInstrumentSelection(
                track, state, typedEvent.header.tick, channel,
                instrumentSelection(InstrumentPerformanceEvent{.bank = address.bank, .program = address.program},
                                    soundBanks),
                options, modulationConversion, false);
          }
          const u8 key = midiKey(typedEvent.key + globalTransposeAt(globalTransposes, typedEvent.header.tick));
          if (shouldRestartSimulatedVibratoForNote(typedEvent, state)) {
            restartSimulatedVibratoForNote(track, state, typedEvent.header.tick, channel);
          } else {
            releaseHeldPitchLfoOutputs(track, state, typedEvent.header.tick, channel);
          }
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            if (shouldRestartSimulatedTremoloForNote(typedEvent, state)) {
              restartSimulatedTremoloForNote(track, state, typedEvent.header.tick, channel, options,
                                             modulationConversion);
            } else if (releaseHeldLfoOutput(state.tremolo) && state.simulatedTremoloGain != 1.0) {
              state.simulatedTremoloGain = 1.0;
              addCombinedExpression(track, state, typedEvent.header.tick, channel, options, modulationConversion);
            }
          }
          if (shouldRestartSimulatedPanForNote(typedEvent, state)) {
            restartSimulatedPanForNote(track, state, typedEvent.header.tick, channel, options);
          }
          if (extendPreviousNote(track, state, typedEvent, channel)) {
            return;
          }
          if (options.terminatePreviousVoice && typedEvent.restartsEnvelope && state.lastNoteIndex) {
            // Silence the old voice before bank, range, controller, or bend
            // state for this attack can affect it.
            addController(track, typedEvent.header.tick, channel, MidiController::AllSoundOff, 0,
                          kVoiceTerminationPriority);
          }
          if (!state.levelEmitted &&
              (state.levelHeadroom != 1.0 || state.sourceLevelGain != 1.0 || state.panLevelGain != 1.0)) {
            addCombinedLevel(track, state, typedEvent.header.tick, channel, options);
          }
          state.lastNoteIndex = track.events.size();
          u32 duration = typedEvent.durationTicks;
          if (typedEvent.maximumDurationMilliseconds) {
            duration = std::min(duration, globalTempos.durationTicksForMilliseconds(
                                              typedEvent.header.tick, *typedEvent.maximumDurationMilliseconds));
          }
          track.events.push_back(
              midi::note(typedEvent.header.tick, channel, key, midiVelocity(typedEvent.linearVelocity), duration));
        } else if constexpr (std::is_same_v<TypedEvent, TempoPerformanceEvent>) {
          // Tempo is song-wide. Effective changes are written once on the
          // first MIDI track after all source tracks have been lowered.
        } else if constexpr (std::is_same_v<TypedEvent, TimeSignaturePerformanceEvent>) {
          // Standard MIDI treats time signatures as global metadata. They are collected
          // once and written to the first MIDI track by renderMidiSequence.
        } else if constexpr (std::is_same_v<TypedEvent, InstrumentPerformanceEvent>) {
          const auto selection = instrumentSelection(typedEvent, soundBanks);
          applyInstrumentSelection(track, state, typedEvent.header.tick, channel, selection, options,
                                   modulationConversion, true);
        } else if constexpr (std::is_same_v<TypedEvent, LevelPerformanceEvent>) {
          state.sourceLevelGain = typedEvent.linearGain;
          state.sourceLevelQuantization = typedEvent.sourceQuantization;
          addCombinedLevel(track, state, typedEvent.header.tick, channel, options, automationState);
        } else if constexpr (std::is_same_v<TypedEvent, ExpressionPerformanceEvent>) {
          state.sourceExpressionGain = typedEvent.linearGain;
          state.sourceExpressionQuantization = typedEvent.sourceQuantization;
          addCombinedExpression(track, state, typedEvent.header.tick, channel, options, modulationConversion,
                                automationState);
        } else if constexpr (std::is_same_v<TypedEvent, PanPerformanceEvent>) {
          state.sourcePanPosition = typedEvent.stereoPosition;
          state.sourcePanLinearGain = typedEvent.hasLinearGain ? typedEvent.linearGain : 1.0;
          state.sourcePanLaw = typedEvent.law;
          addCombinedPan(track, state, typedEvent.header.tick, channel, options, automationState,
                         automationState == nullptr);
        } else if constexpr (std::is_same_v<TypedEvent, ChannelPanPerformanceEvent>) {
          // MIDI/SF2 already applies CC10 to each voice's intrinsic region pan.
          // Preserve that native composition without aggregate-pan gain repair.
          state.sourcePanPosition = 0.0;
          state.sourcePanLinearGain = 1.0;
          state.sourcePanLaw = PanLaw::Unspecified;
          state.simulatedPanOffset = 0.0;
          state.panLfo = {};
          const u8 value = data7(std::clamp(typedEvent.position, 0.0, 1.0) * 127.0);
          addPan(track, automationState, typedEvent.header.tick, channel, value);
          state.lastPanValue = value;
          if (state.panLevelGain != 1.0) {
            state.panLevelGain = 1.0;
            addCombinedLevel(track, state, typedEvent.header.tick, channel, options, automationState);
          }
        } else if constexpr (std::is_same_v<TypedEvent, StereoBalancePerformanceEvent>) {
          const LoweredStereoBalance lowered = lowerStereoBalance(typedEvent.leftGain, typedEvent.rightGain);
          const double left = std::abs(typedEvent.leftGain);
          const double right = std::abs(typedEvent.rightGain);
          const double sum = left + right;
          state.sourcePanPosition = sum == 0.0 ? 0.0 : (right - left) / sum;
          state.sourcePanLinearGain = sum;
          state.sourcePanLaw = PanLaw::Unspecified;
          addPan(track, automationState, typedEvent.header.tick, channel, lowered.pan);
          state.lastPanValue = lowered.pan;
          state.panLevelGain = lowered.gain;
          addCombinedLevel(track, state, typedEvent.header.tick, channel, options, automationState);
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
          if (!state.lastReverbValue || *state.lastReverbValue != value) {
            addController(track, typedEvent.header.tick, channel, MidiController::Reverb, value);
            state.lastReverbValue = value;
          }
        } else if constexpr (std::is_same_v<TypedEvent, MonoModePerformanceEvent>) {
          addController(track, typedEvent.header.tick, channel, MidiController::MonoMode, typedEvent.channels);
        } else if constexpr (std::is_same_v<TypedEvent, TuningPerformanceEvent>) {
          if (options.tuning == MidiTuningRendering::CoarseAndFineTune) {
            const s32 coarse = std::clamp<s32>(static_cast<s32>(typedEvent.cents / 100.0), -64, 63);
            addCoarseTune(track, typedEvent.header.tick, channel, static_cast<s8>(coarse));
            addFineTune(track, typedEvent.header.tick, channel, typedEvent.cents - coarse * 100.0);
          }
          const double bend = tuningBendSemitones(typedEvent.cents, options.tuning);
          if (bend != state.tuningBendSemitones) {
            state.tuningBendSemitones = bend;
            addCurrentPitchBend(track, state, typedEvent.header.tick, channel, modulationConversion, false);
          }
        } else if constexpr (std::is_same_v<TypedEvent, GlobalTransposePerformanceEvent>) {
          // Global transpose changes how later notes and portamento controls are written. It does not
          // become a MIDI event itself.
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendPerformanceEvent>) {
          state.pitchBendLayers.apply(typedEvent);
          addCurrentPitchBend(track, state, typedEvent.header.tick, channel, modulationConversion, false);
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendRangePerformanceEvent>) {
          state.pitchBendContext.setSourceRangeCents(typedEvent.cents);
          refreshPitchBendRange(track, state, typedEvent.header.tick, channel,
                                effectivePitchBendRangeCents(state, modulationConversion), modulationConversion);
        } else if constexpr (std::is_same_v<TypedEvent, VibratoDelayPerformanceEvent>) {
          setLfoDelay(pitchLfo(state, kPrimaryPitchBendLayer).oscillator, typedEvent.header.tick, typedEvent.delayTicks,
                      typedEvent.milliseconds, typedEvent.tempoRelative, typedEvent.updateMode);
          if (modulationConversion != ModulationConversionPolicy::SequenceEventSimulation) {
            addController(track, typedEvent.header.tick, channel, MidiController::VibratoDelay,
                          vibratoDelayControllerValue(typedEvent, modulationProfile));
          }
        } else if constexpr (std::is_same_v<TypedEvent, TremoloDelayPerformanceEvent>) {
          const auto fallback = typedEvent.milliseconds ? LfoInitialPhaseFallback::Zero
                                                        : LfoInitialPhaseFallback::UnipolarTremoloNominalGain;
          setLfoDelay(state.tremolo, typedEvent.header.tick, typedEvent.delayTicks, typedEvent.milliseconds,
                      typedEvent.tempoRelative, typedEvent.updateMode, fallback);
          if (modulationConversion != ModulationConversionPolicy::SequenceEventSimulation) {
            addController(track, typedEvent.header.tick, channel, MidiController::TremoloDelay,
                          tremoloDelayControllerValue(typedEvent, modulationProfile));
          }
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoPerformanceEvent>) {
          midi::appendController14(track, typedEvent.header.tick, channel, MidiController::PortamentoTime,
                                   data14(typedEvent.timeMilliseconds), true);
          if (typedEvent.previousKey) {
            const double previousKey =
                *typedEvent.previousKey + globalTransposeAt(globalTransposes, typedEvent.header.tick);
            addController(track, typedEvent.header.tick, channel, MidiController::PortamentoControl,
                          midiKey(previousKey));
          }
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoEnablePerformanceEvent>) {
          addController(track, typedEvent.header.tick, channel, MidiController::Portamento,
                        typedEvent.enabled ? 127 : 0);
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoTimePerformanceEvent>) {
          addController(track, typedEvent.header.tick, channel, MidiController::PortamentoTime,
                        data7(typedEvent.timeMilliseconds));
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoControlPerformanceEvent>) {
          const double previousKey =
              typedEvent.previousKey + globalTransposeAt(globalTransposes, typedEvent.header.tick);
          addController(track, typedEvent.header.tick, channel, MidiController::PortamentoControl,
                        midiKey(previousKey));
        } else if constexpr (std::is_same_v<TypedEvent, LegatoPedalPerformanceEvent>) {
          addController(track, typedEvent.header.tick, channel, MidiController::Legato, typedEvent.enabled ? 127 : 0);
        } else if constexpr (std::is_same_v<TypedEvent, ModulationPerformanceEvent>) {
          const double normalizedAmount = modulationControllerAmount(typedEvent, modulationProfile);
          const u8 value = midiNormalized7(normalizedAmount);
          const bool pitchTarget = typedEvent.target == ModulationPerformanceTarget::VibratoDepth ||
                                   typedEvent.target == ModulationPerformanceTarget::VibratoRate;
          if (pitchTarget && (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation ||
                              typedEvent.pitchLayer != kPrimaryPitchBendLayer)) {
            auto& lfo = pitchLfo(state, typedEvent.pitchLayer).oscillator;
            configureLfo(lfo, typedEvent.header.tick, typedEvent);
            if (typedEvent.target == ModulationPerformanceTarget::VibratoDepth) {
              setSimulatedVibratoDepth(
                  track, state, typedEvent.header.tick, channel,
                  typedEvent.pitchDepthSemitones.value_or(std::clamp(typedEvent.amount, 0.0, 1.0) * 2.0),
                  typedEvent.context.zeroDepthBehavior, typedEvent.pitchLayer);
              refreshPitchBendRange(track, state, typedEvent.header.tick, channel,
                                    effectivePitchBendRangeCents(state, modulationConversion), modulationConversion);
              sampleRestartedVibrato(track, state, typedEvent, channel);
            } else {
              refreshPitchBendRange(track, state, typedEvent.header.tick, channel,
                                    effectivePitchBendRangeCents(state, modulationConversion), modulationConversion);
              if (state.lastPitchBendValue) {
                addCurrentPitchBend(track, state, typedEvent.header.tick, channel, modulationConversion, false);
              }
            }
            return;
          }
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            switch (typedEvent.target) {
              case ModulationPerformanceTarget::TremoloDepth: {
                const bool physicalDecibels = typedEvent.volumeDepthDecibels.has_value();
                const bool physicalLinearGain = typedEvent.volumeDepthLinearGain.has_value();
                const auto fallback = !typedEvent.context.shape && !physicalDecibels && !physicalLinearGain
                                          ? LfoInitialPhaseFallback::UnipolarTremoloNominalGain
                                          : LfoInitialPhaseFallback::Zero;
                configureLfo(state.tremolo, typedEvent.header.tick, typedEvent, fallback);
                const auto unit = physicalDecibels
                                      ? RenderTrackState::TremoloDepthUnit::Decibels
                                      : (physicalLinearGain ? RenderTrackState::TremoloDepthUnit::LinearGain
                                                            : RenderTrackState::TremoloDepthUnit::LegacyUnipolar);
                setSimulatedTremoloDepth(track, state, typedEvent.header.tick, channel,
                                         physicalDecibels
                                             ? *typedEvent.volumeDepthDecibels
                                             : (physicalLinearGain ? *typedEvent.volumeDepthLinearGain
                                                                   : std::clamp(typedEvent.amount, 0.0, 1.0) * 0.5),
                                         unit, typedEvent.context.tremoloGainMode, typedEvent.context.zeroDepthBehavior,
                                         options, modulationConversion);
                break;
              }
              case ModulationPerformanceTarget::TremoloRate:
                configureLfo(state.tremolo, typedEvent.header.tick, typedEvent,
                             typedEvent.context.shape ? LfoInitialPhaseFallback::Zero
                                                      : LfoInitialPhaseFallback::UnipolarTremoloNominalGain);
                break;
              case ModulationPerformanceTarget::PanDepth:
                configureLfo(state.panLfo, typedEvent.header.tick, typedEvent);
                setSimulatedPanDepth(track, state, typedEvent.header.tick, channel,
                                     typedEvent.panDepth.value_or(normalizedAmount), options);
                break;
              case ModulationPerformanceTarget::PanRate:
                configureLfo(state.panLfo, typedEvent.header.tick, typedEvent);
                break;
              case ModulationPerformanceTarget::VibratoDepth:
              case ModulationPerformanceTarget::VibratoRate:
                break;
            }
            return;
          }
          if (typedEvent.target == ModulationPerformanceTarget::PanDepth ||
              typedEvent.target == ModulationPerformanceTarget::PanRate) {
            configureLfo(state.panLfo, typedEvent.header.tick, typedEvent);
            if (typedEvent.target == ModulationPerformanceTarget::PanDepth) {
              setSimulatedPanDepth(track, state, typedEvent.header.tick, channel,
                                   typedEvent.panDepth.value_or(normalizedAmount), options);
            }
            return;
          }
          switch (typedEvent.target) {
            case ModulationPerformanceTarget::VibratoDepth:
              addController(track, typedEvent.header.tick, channel, MidiController::Modulation, value, 20,
                            normalizedAmount);
              break;
            case ModulationPerformanceTarget::VibratoRate:
              addController(track, typedEvent.header.tick, channel, MidiController::VibratoRate, value, 20,
                            normalizedAmount);
              break;
            case ModulationPerformanceTarget::TremoloDepth:
              addController(track, typedEvent.header.tick, channel, MidiController::TremoloDepth, value, 20,
                            normalizedAmount);
              break;
            case ModulationPerformanceTarget::TremoloRate:
              addController(track, typedEvent.header.tick, channel, MidiController::TremoloRate, value, 20,
                            normalizedAmount);
              break;
            case ModulationPerformanceTarget::PanDepth:
            case ModulationPerformanceTarget::PanRate:
              break;
          }
        } else if constexpr (std::is_same_v<TypedEvent, MarkerPerformanceEvent>) {
          track.events.push_back(midi::meta(typedEvent.header.tick, 0x06,
                                            std::vector<u8>(typedEvent.text.begin(), typedEvent.text.end()), 90));
        }
      },
      event);
}

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
  std::vector<bool> renderedTempoPoints(globalTempoPoints.size(), false);
  const PerformanceSequence loweredPerformance =
      lowerMidiPerformanceAutomation(performance, options, globalTempos, soundBanks);
  MidiSequence sequence{
      .timebase = loweredPerformance.timebase,
      .diagnostics = loweredPerformance.diagnostics,
  };
  sequence.tracks.reserve(loweredPerformance.tracks.size());
  const PerformanceTimelines timelines = buildPerformanceTimelines(loweredPerformance);
  const std::vector<GlobalTransposeChange> globalTransposes = collectGlobalTransposeChanges(timelines);
  const std::vector<MidiEvent> globalTimeSignatures = collectGlobalTimeSignatures(timelines);
  const double levelHeadroom = panLevelHeadroom(timelines);

  for (size_t trackIndex = 0; trackIndex < loweredPerformance.tracks.size(); ++trackIndex) {
    const auto& performanceTrack = loweredPerformance.tracks[trackIndex];
    MidiTrack midiTrack{
        .name = performanceTrack.name.empty() ? "Track " + std::to_string(performanceTrack.sourceTrackNumber)
                                              : performanceTrack.name,
    };
    RenderTrackState renderState;
    renderState.levelHeadroom = levelHeadroom;
    std::unordered_map<PerformanceAutomationId, MidiControllerState> automationControllerStates;
    const auto pitchBendRangeChanges = planVoicePitchBendRanges(timelines[trackIndex], options.tuning, soundBanks);
    size_t nextPitchBendRangeChange = 0;
    const auto assignment = midiChannelAssignment(trackIndex, options);
    if (assignment.port > 255) {
      sequence.diagnostics.push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "MIDI port number exceeded the Standard MIDI File port meta-event range",
      });
    }
    if (options.writePortMetaEvents) {
      midiTrack.events.push_back(midi::meta(0, 0x21, {midiPortByte(assignment.port)}, -5));
    }
    applyInstrumentPitchBendRange(midiTrack, renderState, 0, assignment.channel,
                                  instrumentSelection(InstrumentPerformanceEvent{}, soundBanks).pitchBendRangeCents,
                                  modulationConversion);
    for (const auto* event : timelines[trackIndex]) {
      const auto& header = performanceEventHeader(*event);
      while (nextPitchBendRangeChange < pitchBendRangeChanges.size() &&
             std::tie(pitchBendRangeChanges[nextPitchBendRangeChange].tick,
                      pitchBendRangeChanges[nextPitchBendRangeChange].sequence) <=
                 std::tie(header.tick, header.sequence)) {
        const auto& change = pitchBendRangeChanges[nextPitchBendRangeChange++];
        if (change.tick != 0) {
          // Finish the previous voice before changing the channel sensitivity.
          flushSimulatedVibrato(midiTrack, renderState, change.tick - 1, assignment.channel, globalTempos);
        }
        applyVoicePitchBendRangeChange(midiTrack, renderState, change, assignment.channel, modulationConversion);
      }

      const auto* note = std::get_if<NotePerformanceEvent>(event);
      flushSimulatedVibrato(midiTrack, renderState, header.tick, assignment.channel, globalTempos, note);
      u64 otherFlushTick = header.tick;
      if (note != nullptr &&
          ((modulationConversion == ModulationConversionPolicy::SequenceEventSimulation &&
            shouldRestartSimulatedTremoloForNote(*note, renderState)) ||
           shouldRestartSimulatedPanForNote(*note, renderState)) &&
          otherFlushTick != 0) {
        --otherFlushTick;
      }
      if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
        flushSimulatedTremolo(midiTrack, renderState, otherFlushTick, assignment.channel, globalTempos, options,
                              modulationConversion);
      }
      flushSimulatedPan(midiTrack, renderState, otherFlushTick, assignment.channel, globalTempos, options);
      if (trackIndex == 0) {
        if (const auto* tempo = std::get_if<TempoPerformanceEvent>(event);
            tempo != nullptr && globalTempos.contains(*tempo)) {
          midiTrack.events.push_back(tempoEvent(tempo->header.tick, tempo->microsecondsPerQuarter));
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
                   globalTransposes, globalTempos, options, modulationConversion, soundBanks, modulationProfile,
                   automationState);
    }
    u64 endTick = performanceTrack.endTick;
    flushSimulatedVibrato(midiTrack, renderState, endTick, assignment.channel, globalTempos);
    if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
      flushSimulatedTremolo(midiTrack, renderState, endTick, assignment.channel, globalTempos, options,
                            modulationConversion);
    }
    flushSimulatedPan(midiTrack, renderState, endTick, assignment.channel, globalTempos, options);
    if (trackIndex == 0) {
      for (size_t index = 0; index < globalTempoPoints.size(); ++index) {
        if (renderedTempoPoints[index]) {
          continue;
        }
        const auto& tempo = globalTempoPoints[index];
        midiTrack.events.push_back(tempoEvent(tempo.tick, tempo.microsecondsPerQuarter));
        endTick = std::max(endTick, tempo.tick);
      }
      midiTrack.events.insert(midiTrack.events.end(), globalTimeSignatures.begin(), globalTimeSignatures.end());
      for (const auto& timeSignature : globalTimeSignatures) {
        endTick = std::max(endTick, timeSignature.tick);
      }
    }
    midiTrack.endTick = endTick;
    sequence.tracks.push_back(std::move(midiTrack));
  }

  return sequence;
}

}  // namespace vgmtrans::core

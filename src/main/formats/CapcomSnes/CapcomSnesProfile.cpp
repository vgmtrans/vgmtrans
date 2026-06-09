/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/CapcomSnes/CapcomSnesProfile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr int kVolumeCurveLastIndex = 16;
constexpr int kTremoloPeakScalarV1 = 255;
constexpr int kTremoloPeakScalarV2 = 250;
constexpr double kTremoloMuteFloorCentibels = 960.0;
constexpr int kTremoloHalfDepthCentibels = 484;
constexpr double kLfoStepHz = 1000.0 / 16384.0;
constexpr double kVibratoBaseHz = kLfoStepHz;
constexpr double kVibratoMaxHz = 255.0 * kLfoStepHz;

constexpr std::array<u8, 17> kVolumeTable{
    0x00, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4c, 0x59, 0x66,
    0x73, 0x80, 0x8c, 0x99, 0xb3, 0xcc, 0xe6, 0xff};

constexpr std::array<u8, 22> kPanTable{
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
    0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f};

#ifndef M_PI_2
constexpr double kPiOverTwo = 1.57079632679489661923;
#else
constexpr double kPiOverTwo = M_PI_2;
#endif

struct PanConversionResult {
  u8 midiPan = 64;
  double volumeScale = 1.0;
};

[[nodiscard]] u32 capcomLength(u32 rawDuration) {
  if (rawDuration == 0 || rawDuration > 7) {
    return 0;
  }
  return 192u >> (7u - rawDuration);
}

[[nodiscard]] u16 percentAmpTo14BitMidi(double percent) {
  return static_cast<u16>(std::clamp<int>(static_cast<int>(std::lround(16383.0 * std::sqrt(percent))), 0, 16383));
}

[[nodiscard]] u8 percentAmpTo7BitMidi(double percent) {
  return static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(127.0 * std::sqrt(percent))), 0, 127));
}

[[nodiscard]] u8 percentPanToMidi(double percent) {
  u8 midiPan = static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(percent * 126.0)), 0, 126));
  if (midiPan != 0) {
    ++midiPan;
  }
  return midiPan;
}

void midiPanToVolumeBalance(u8 midiPan, double& left, double& right) {
  if (midiPan == 0 || midiPan == 1) {
    left = 1.0;
    right = 0.0;
    return;
  }
  if (midiPan == 64) {
    left = right = std::sqrt(2.0) / 2.0;
    return;
  }
  if (midiPan == 127) {
    left = 0.0;
    right = 1.0;
    return;
  }

  const double percentPan = (midiPan - 1) / 126.0;
  left = std::cos(kPiOverTwo * percentPan);
  right = std::sin(kPiOverTwo * percentPan);
}

[[nodiscard]] u8 linearPercentPanToMidi(double percent, double* volumeScale) {
  u8 midiPan = 64;
  double scale = 1.0;

  if (percent == 0.0) {
    midiPan = 0;
  } else if (percent == 0.5) {
    midiPan = 64;
    scale = 1.0 / std::sqrt(2.0);
  } else if (percent == 1.0) {
    midiPan = 127;
  } else {
    const double arcPan = std::atan2(percent, 1.0 - percent);
    midiPan = percentPanToMidi(arcPan / kPiOverTwo);

    double midiLeft = 0.0;
    double midiRight = 0.0;
    midiPanToVolumeBalance(midiPan, midiLeft, midiRight);
    scale = 1.0 / (midiLeft + midiRight);
  }

  if (volumeScale != nullptr) {
    *volumeScale = scale;
  }
  return midiPan;
}

[[nodiscard]] u8 linear7BitPanToMidi(u8 rawPan, double* volumeScale) {
  if (rawPan == 127) {
    ++rawPan;
  }
  return linearPercentPanToMidi(rawPan / 128.0, volumeScale);
}

[[nodiscard]] u8 volumeBalanceToMidiPan(double left, double right, double* volumeScale) {
  u8 midiPan = 64;
  if (right == 0.0) {
    midiPan = 0;
  } else if (left == right) {
    midiPan = 64;
  } else if (left == 0.0) {
    midiPan = 127;
  } else {
    midiPan = linearPercentPanToMidi(right / (left + right), nullptr);
  }

  if (volumeScale != nullptr) {
    double midiLeft = 0.0;
    double midiRight = 0.0;
    midiPanToVolumeBalance(midiPan, midiLeft, midiRight);
    *volumeScale = (left + right) / (midiLeft + midiRight);
  }
  return midiPan;
}

[[nodiscard]] int interpolatePanFactor(u16 panPosition) {
  const int panIndex = panPosition >> 8;
  const int panRate = panPosition & 0xff;
  const int lower = kPanTable[panIndex];
  const int upper = kPanTable[panIndex + 1];
  return lower + ((upper - lower) * panRate >> 8);
}

[[nodiscard]] PanConversionResult calculatePanV2(u8 biasedPan) {
  const u16 rightPanPosition = static_cast<u16>(biasedPan) * 20;
  const u16 leftPanPosition = 0x1400 - rightPanPosition;
  const double volumeLeft = interpolatePanFactor(leftPanPosition) / 128.0;
  const double volumeRight = interpolatePanFactor(rightPanPosition) / 128.0;

  PanConversionResult result;
  result.midiPan = volumeBalanceToMidiPan(volumeLeft, volumeRight, &result.volumeScale);
  return result;
}

[[nodiscard]] int interpolateVolumeCurve(int curveIndex, int curveFraction) {
  if (curveIndex >= kVolumeCurveLastIndex) {
    return kVolumeTable[kVolumeCurveLastIndex];
  }
  const int lower = kVolumeTable[curveIndex];
  const int upper = kVolumeTable[curveIndex + 1];
  return lower + (((upper - lower) * curveFraction) >> 8);
}

[[nodiscard]] int calculateVolumeScalar(u8 sourceVolume) {
  if (sourceVolume >= 0x80) {
    return kVolumeTable[kVolumeCurveLastIndex];
  }
  const int curveIndex = sourceVolume >> 3;
  const int curveFraction = ((sourceVolume & 0x07) << 5) | 0x1f;
  return interpolateVolumeCurve(curveIndex, curveFraction);
}

[[nodiscard]] double calculateVolumeV2(u8 sourceVolume) {
  return static_cast<double>(calculateVolumeScalar(sourceVolume)) / 255.0;
}

[[nodiscard]] int calculateTremoloScalarAtTroughV1(int sourceDepth) {
  const int depth = sourceDepth & 0x7f;
  if (depth == 0) {
    return 255;
  }
  return 255 - ((2 * depth * 255) >> 8);
}

[[nodiscard]] int calculateTremoloScalarAtTroughV2(int sourceDepth) {
  sourceDepth = std::clamp(sourceDepth, 0, 127);
  if (sourceDepth == 0) {
    return 250;
  }
  if (sourceDepth == 127) {
    return 0;
  }

  const int inverseCurvePosition = 0x7e81 - sourceDepth * 255;
  const int scaledCurvePosition = inverseCurvePosition >> 3;
  return interpolateVolumeCurve(scaledCurvePosition >> 8, scaledCurvePosition & 0xff);
}

[[nodiscard]] u8 tremoloDepthToMidiValue(int sourceDepth, CapcomSnesEngineVersion version) {
  const int peakScalar = version == CapcomSnesEngineVersion::v1BgmInList ? kTremoloPeakScalarV1 : kTremoloPeakScalarV2;
  const int troughScalar = version == CapcomSnesEngineVersion::v1BgmInList
                               ? calculateTremoloScalarAtTroughV1(sourceDepth)
                               : calculateTremoloScalarAtTroughV2(sourceDepth);

  double depthCentibels = kTremoloMuteFloorCentibels;
  if (troughScalar > 0) {
    depthCentibels = 200.0 * std::log10(peakScalar / static_cast<double>(troughScalar));
    depthCentibels = std::clamp(depthCentibels, 0.0, kTremoloMuteFloorCentibels);
  }

  const int midiValue = static_cast<int>(
      std::floor(depthCentibels * 128.0 / (2.0 * static_cast<double>(kTremoloHalfDepthCentibels)) + 0.5));
  return static_cast<u8>(std::clamp(midiValue, 0, 127));
}

[[nodiscard]] double hertzToCents(double hertz) {
  return 1200.0 * std::log2(hertz / 440.0) + 6900.0;
}

[[nodiscard]] u8 midiValueForHertzInRange(double hertz, double minHertz, double maxHertz) {
  if (hertz <= 0.0 || minHertz <= 0.0 || maxHertz <= minHertz) {
    return 0;
  }
  const double minCents = hertzToCents(minHertz);
  const double maxCents = hertzToCents(maxHertz);
  const double currentCents = hertzToCents(hertz);
  const double value = (currentCents - minCents) * 127.0 / (maxCents - minCents);
  return static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 127));
}

[[nodiscard]] u8 lfoRateByteToMidiValue(u8 rate) {
  if (rate == 0) {
    return 0;
  }
  return midiValueForHertzInRange(static_cast<double>(rate) * kLfoStepHz, kVibratoBaseHz, kVibratoMaxHz);
}

}  // namespace

CapcomSnesProfile::CapcomSnesProfile(CapcomSnesEngineVersion version) : version_(version) {
}

u32 CapcomSnesProfile::restTicks(const RestCommand& command, const TrackState&) const {
  return capcomLength(command.rawDuration);
}

NoteTiming CapcomSnesProfile::noteTiming(const NoteCommand& command, const TrackState& state) const {
  const u32 length = capcomLength(command.rawDuration);
  u32 duration = length * state.durationRate;
  if (duration == 0) {
    duration = length;
  } else {
    duration = (duration + 0x80) >> 8;
    if (duration == 0) {
      duration = 1;
    }
  }

  const s32 key = std::clamp<s32>(static_cast<s32>(command.key) - 1 + state.transpose, 0, 127);
  return NoteTiming{
      .key = static_cast<u8>(key),
      .velocity = 127,
      .soundingTicks = duration,
      .advanceTicks = length,
  };
}

void CapcomSnesProfile::applyDuration(const DurationCommand& command, TrackState& state) const {
  state.durationRate = command.rawValue;
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerTempo(
    const TempoCommand& command,
    const TrackState& state) const {
  const u32 microsecondsPerQuarter = command.rawValue == 0
                                         ? 60000000
                                         : static_cast<u32>(std::lround(60000000.0 * 256.0 / (125.0 * command.rawValue)));
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = microsecondsPerQuarter,
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerVolume(
    const VolumeCommand& command,
    const TrackState& state) const {
  if (version_ == CapcomSnesEngineVersion::v1BgmInList) {
    return {Volume{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<u8>(std::min<u32>(command.rawValue >> 1, 127)),
    }};
  }

  return {Volume14{
      .tick = state.tick,
      .channel = state.channel,
      .value = percentAmpTo14BitMidi(calculateVolumeV2(static_cast<u8>(command.rawValue))),
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerPan(
    const PanCommand& command,
    const TrackState& state) const {
  const auto biasedPan = static_cast<u8>(command.rawValue + 0x80);
  PanConversionResult pan;
  if (version_ == CapcomSnesEngineVersion::v1BgmInList) {
    pan.midiPan = linear7BitPanToMidi(biasedPan >> 1, &pan.volumeScale);
  } else {
    pan = calculatePanV2(biasedPan);
  }

  return {
      Pan{
          .tick = state.tick,
          .channel = state.channel,
          .value = pan.midiPan,
      },
      Expression{
          .tick = state.tick,
          .channel = state.channel,
          .value = percentAmpTo7BitMidi(pan.volumeScale),
      },
  };
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerMasterVolume(
    const MasterVolumeCommand& command,
    const TrackState& state) const {
  if (version_ == CapcomSnesEngineVersion::v1BgmInList) {
    return {MasterVolume{
        .tick = state.tick,
        .value = static_cast<u16>(std::min<u32>((command.rawValue >> 1) * 129, 0x3fff)),
    }};
  }

  return {MasterVolume{
      .tick = state.tick,
      .value = percentAmpTo14BitMidi(calculateVolumeV2(static_cast<u8>(command.rawValue))),
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerReverb(
    const ReverbCommand& command,
    const TrackState& state) const {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>((command.rawValue & 1) != 0 ? 40 : 0),
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerTuning(
    const TuningCommand& command,
    const TrackState& state) const {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = static_cast<s16>(std::lround(command.rawValue * 100.0 / 256.0)),
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerLfo(
    const LfoCommand& command,
    const TrackState& state) const {
  switch (command.rawType) {
    case 0:
      return {VibratoDepth{
          .tick = state.tick,
          .channel = state.channel,
          .value = static_cast<u8>(command.rawAmount & 0x7f),
      }};
    case 1:
      return {TremoloDepth{
          .tick = state.tick,
          .channel = state.channel,
          .value = tremoloDepthToMidiValue(static_cast<int>(command.rawAmount), version_),
      }};
    case 2: {
      const u8 rate = lfoRateByteToMidiValue(static_cast<u8>(command.rawAmount));
      return {
          VibratoFrequency{
              .tick = state.tick,
              .channel = state.channel,
              .value = rate,
          },
          TremoloFrequency{
              .tick = state.tick,
              .channel = state.channel,
              .value = rate,
          },
      };
    }
    default:
      return {};
  }
}

void registerCapcomSnesProfile(SequencerProfileRegistry& registry) {
  registry.add("CapcomSnes", [] {
    return std::make_unique<CapcomSnesProfile>(CapcomSnesEngineVersion::v3BgmFixedLocation);
  });
}

}  // namespace vgmtrans::formats::capcom_snes

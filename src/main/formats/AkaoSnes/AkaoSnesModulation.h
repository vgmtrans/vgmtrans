/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Modulation.h"

namespace akao_snes::modulation {

inline constexpr uint8_t kDefaultTempo = 0x20;
inline constexpr uint8_t kMinTimer0Frequency = 0x24;
inline constexpr uint8_t kDefaultTimer0Frequency = 0x27;
inline constexpr uint8_t kMaxTimer0Frequency = 0x2a;
inline constexpr uint8_t kVibratoDelayController = 93;
inline constexpr uint8_t kTremoloDepthController = 92;
inline constexpr uint8_t kTremoloRateController = 94;
inline constexpr uint8_t kTremoloDelayController = 95;

inline constexpr uint16_t effectiveRateFrames(uint8_t rate) {
  return (rate == 0) ? 256 : rate;
}

inline double frameRateHz(uint8_t timer0Frequency) {
  return 8000.0 / timer0Frequency;
}

inline double lfoRateHz(uint8_t rate, uint8_t timer0Frequency) {
  return frameRateHz(timer0Frequency) / (2.0 * effectiveRateFrames(rate));
}

inline uint16_t lfoStep(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const uint16_t frames = effectiveRateFrames(rate);
  const uint8_t magnitude = static_cast<uint8_t>((depth & 0x3f) + 1);
  const uint16_t step = static_cast<uint16_t>(64 * magnitude / frames);
  return static_cast<uint16_t>(4 * std::max<uint16_t>(1, step));
}

inline double phaseHighByteAmplitude(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0.0;
  }

  return lfoStep(rate, depth) * effectiveRateFrames(rate) / 256.0;
}

inline double vibratoDepthCentsForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  const double ratio = 15.0 * amplitude / 32768.0;
  const double centsUp = 1200.0 * std::log2(1.0 + ratio);
  const double centsDown = -1200.0 * std::log2(1.0 - ratio);
  return std::max(centsUp, centsDown);
}

inline double vibratoDepthCents(uint8_t rate, uint8_t depth) {
  return vibratoDepthCentsForAmplitude(phaseHighByteAmplitude(rate, depth));
}

inline double tremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  const double troughScale = std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0));
  return -20.0 * std::log10(troughScale);
}

inline double tremoloDepthDb(uint8_t rate, uint8_t depth) {
  return tremoloDepthDbForAmplitude(phaseHighByteAmplitude(rate, depth));
}

inline double delaySeconds(uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  if (delay == 0) {
    return 0.0;
  }

  const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
  return delay * (256.0 / (frameRateHz(timer0Frequency) * safeTempo));
}

inline constexpr double kMinLfoRateHz = 8000.0 / kMaxTimer0Frequency / (2.0 * 256.0);
inline constexpr double kMaxLfoRateHz = 8000.0 / kMinTimer0Frequency / 2.0;
inline const double kMaxVibratoDepthCents = vibratoDepthCentsForAmplitude(64.0);
inline const double kMaxTremoloDepthDb = tremoloDepthDbForAmplitude(64.0);
inline const double kMaxDelaySeconds = 255.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);

inline VibratoModulationSpec vibratoSpec() {
  return {
      kMaxVibratoDepthCents,
      kMinLfoRateHz,
      kMaxLfoRateHz,
      DelayRange {
          0.0,
          kMaxDelaySeconds,
      },
  };
}

inline TremoloModulationSpec tremoloSpec() {
  return {
      kMaxTremoloDepthDb,
      kMinLfoRateHz,
      kMaxLfoRateHz,
      TremoloGainMode::BipolarAroundNominal,
      DelayRange {
          0.0,
          kMaxDelaySeconds,
      },
      ModSource::Effects4Depth,
      ModSource::Effects2Depth,
      ModSource::Effects5Depth,
  };
}

inline uint8_t vibratoDepthMidiValue(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const int midiValue =
      static_cast<int>(std::lround(128.0 * vibratoDepthCents(rate, depth) / kMaxVibratoDepthCents));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

inline uint8_t tremoloDepthMidiValue(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const int midiValue =
      static_cast<int>(std::lround(128.0 * tremoloDepthDb(rate, depth) / kMaxTremoloDepthDb));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

inline uint8_t rateMidiValue(uint8_t rate, uint8_t timer0Frequency) {
  return midiValueForHertzInRange(lfoRateHz(rate, timer0Frequency), kMinLfoRateHz, kMaxLfoRateHz);
}

inline uint8_t delayMidiValue(uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  return midiValueForSecondsInRange(delaySeconds(delay, tempo, timer0Frequency), 0.0, kMaxDelaySeconds);
}

}  // namespace akao_snes::modulation

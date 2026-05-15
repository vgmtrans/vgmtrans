/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "AkaoSnesFormat.h"
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

inline constexpr bool supportsLfoAutomation(AkaoSnesVersion version) {
  return version == AKAOSNES_V3 || version == AKAOSNES_V4;
}

inline constexpr uint16_t effectiveRateFrames(AkaoSnesVersion version, uint8_t rate) {
  if (version == AKAOSNES_V3) {
    return static_cast<uint16_t>(rate) + 1;
  }

  return (rate == 0) ? 256 : rate;
}

inline constexpr uint8_t modulationMagnitude(AkaoSnesVersion version, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const uint8_t magnitude = static_cast<uint8_t>(depth & 0x3f);
  return (version == AKAOSNES_V3)
      ? static_cast<uint8_t>((magnitude * 2) + 1)
      : static_cast<uint8_t>(magnitude + 1);
}

inline double frameRateHz(uint8_t timer0Frequency) {
  return 8000.0 / timer0Frequency;
}

inline double lfoRateHz(AkaoSnesVersion version, uint8_t rate, uint8_t timer0Frequency) {
  return frameRateHz(timer0Frequency) / (2.0 * effectiveRateFrames(version, rate));
}

inline uint16_t v4LfoStep(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const uint16_t frames = effectiveRateFrames(AKAOSNES_V4, rate);
  const uint8_t magnitude = modulationMagnitude(AKAOSNES_V4, depth);
  const uint16_t step = static_cast<uint16_t>(64 * magnitude / frames);
  return static_cast<uint16_t>(4 * std::max<uint16_t>(1, step));
}

inline double v4PhaseHighByteAmplitude(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0.0;
  }

  return v4LfoStep(rate, depth) * effectiveRateFrames(AKAOSNES_V4, rate) / 256.0;
}

inline double modulationAmplitude(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0.0;
  }

  return (version == AKAOSNES_V3)
      ? modulationMagnitude(version, depth)
      : v4PhaseHighByteAmplitude(rate, depth);
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

inline double vibratoDepthCents(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  return vibratoDepthCentsForAmplitude(modulationAmplitude(version, rate, depth));
}

inline double tremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  const double troughScale = std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0));
  return -20.0 * std::log10(troughScale);
}

inline double tremoloDepthDb(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  return tremoloDepthDbForAmplitude(modulationAmplitude(version, rate, depth));
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
inline const double kMaxV3VibratoDepthCents = vibratoDepthCentsForAmplitude(127.0);
inline const double kMaxV4VibratoDepthCents = vibratoDepthCentsForAmplitude(64.0);
inline const double kMaxV3TremoloDepthDb = tremoloDepthDbForAmplitude(127.0);
inline const double kMaxV4TremoloDepthDb = tremoloDepthDbForAmplitude(64.0);
inline const double kMaxDelaySeconds = 255.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);

inline double maxVibratoDepthCents(AkaoSnesVersion version) {
  return (version == AKAOSNES_V3) ? kMaxV3VibratoDepthCents : kMaxV4VibratoDepthCents;
}

inline double maxTremoloDepthDb(AkaoSnesVersion version) {
  return (version == AKAOSNES_V3) ? kMaxV3TremoloDepthDb : kMaxV4TremoloDepthDb;
}

inline VibratoModulationSpec vibratoSpec(AkaoSnesVersion version) {
  return {
      maxVibratoDepthCents(version),
      kMinLfoRateHz,
      kMaxLfoRateHz,
      DelayRange {
          0.0,
          kMaxDelaySeconds,
      },
  };
}

inline TremoloModulationSpec tremoloSpec(AkaoSnesVersion version) {
  return {
      maxTremoloDepthDb(version),
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

inline uint8_t vibratoDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const int midiValue =
      static_cast<int>(std::lround(128.0 * vibratoDepthCents(version, rate, depth) /
                                   maxVibratoDepthCents(version)));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

inline uint8_t tremoloDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const int midiValue =
      static_cast<int>(std::lround(128.0 * tremoloDepthDb(version, rate, depth) /
                                   maxTremoloDepthDb(version)));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

inline uint8_t rateMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t timer0Frequency) {
  return midiValueForHertzInRange(lfoRateHz(version, rate, timer0Frequency), kMinLfoRateHz, kMaxLfoRateHz);
}

inline uint8_t delayMidiValue(uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  return midiValueForSecondsInRange(delaySeconds(delay, tempo, timer0Frequency), 0.0, kMaxDelaySeconds);
}

}  // namespace akao_snes::modulation

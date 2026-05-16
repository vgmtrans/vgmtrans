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
  return version == AKAOSNES_V1 || version == AKAOSNES_V2 ||
         version == AKAOSNES_V3 || version == AKAOSNES_V4;
}

inline constexpr uint8_t v1RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate >> 1);
}

inline constexpr uint8_t v2RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate & 0x7f);
}

inline constexpr bool isLfoActive(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (!supportsLfoAutomation(version)) {
    return false;
  }

  if (version == AKAOSNES_V2) {
    return v2RateCounter(rate) != 0;
  }

  if (depth == 0) {
    return false;
  }

  return version != AKAOSNES_V1 || v1RateCounter(rate) != 0;
}

inline constexpr uint16_t effectiveRateFrames(AkaoSnesVersion version, uint8_t rate, uint8_t depth = 0) {
  if (version == AKAOSNES_V1) {
    return static_cast<uint16_t>(v1RateCounter(rate)) + 1;
  }

  if (version == AKAOSNES_V2) {
    const uint16_t frames = v2RateCounter(rate);
    return static_cast<uint16_t>((depth & 0x80) != 0 ? frames * 2 : frames);
  }

  if (version == AKAOSNES_V3) {
    return static_cast<uint16_t>(rate) + 1;
  }

  return (rate == 0) ? 256 : rate;
}

inline constexpr uint8_t modulationMagnitude(AkaoSnesVersion version, uint8_t depth) {
  if (version == AKAOSNES_V2) {
    return static_cast<uint8_t>((depth & 0x3f) + 1);
  }

  if (depth == 0) {
    return 0;
  }

  if (version == AKAOSNES_V1) {
    return depth;
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
  const uint16_t frames = effectiveRateFrames(version, rate);
  return (frames == 0) ? 0.0 : frameRateHz(timer0Frequency) / (2.0 * frames);
}

inline double lfoRateHz(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency) {
  const uint16_t frames = effectiveRateFrames(version, rate, depth);
  return (frames == 0) ? 0.0 : frameRateHz(timer0Frequency) / (2.0 * frames);
}

inline double minLfoRateHz(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 128.0);
  }

  if (version == AKAOSNES_V2) {
    return frameRateHz(kMinTimer0Frequency) / (4.0 * 127.0);
  }

  return 8000.0 / kMaxTimer0Frequency / (2.0 * 256.0);
}

inline double maxLfoRateHz(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 2.0);
  }

  if (version == AKAOSNES_V2) {
    return frameRateHz(kMinTimer0Frequency) / 2.0;
  }

  return 8000.0 / kMinTimer0Frequency / 2.0;
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

inline double v2PhaseHighByteAmplitude(uint8_t rate, uint8_t depth) {
  const uint8_t frames = v2RateCounter(rate);
  if (frames == 0) {
    return 0.0;
  }

  const uint16_t step = static_cast<uint16_t>(512 * modulationMagnitude(AKAOSNES_V2, depth) / frames);
  return std::min(128.0, static_cast<double>((step * frames) >> 8));
}

inline double modulationAmplitude(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (version == AKAOSNES_V2) {
    return v2PhaseHighByteAmplitude(rate, depth);
  }

  if (depth == 0) {
    return 0.0;
  }

  return (version == AKAOSNES_V3)
      ? modulationMagnitude(version, depth)
      : v4PhaseHighByteAmplitude(rate, depth);
}

inline uint8_t v1VibratoHighByteAmplitude(uint8_t rate, uint8_t depth) {
  const uint8_t counter = v1RateCounter(rate);
  if (counter == 0 || depth == 0) {
    return 0;
  }

  const uint32_t step = (256u * depth) / counter;
  return static_cast<uint8_t>((step * counter) / 256u);
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

inline double v1VibratoDepthCentsForHighByte(uint8_t amplitude) {
  if (amplitude == 0) {
    return 0.0;
  }

  return 1200.0 * std::log2(1.0 + (amplitude / 3072.0));
}

inline double v2VibratoDepthCents(uint8_t rate, uint8_t depth) {
  const double amplitude = std::min(127.0, v2PhaseHighByteAmplitude(rate, depth));
  if (amplitude <= 0.0) {
    return 0.0;
  }

  if (depth < 0x40) {
    return -1200.0 * std::log2(1.0 - (15.0 * amplitude / 65536.0));
  }

  return 1200.0 * std::log2(1.0 + (15.0 * amplitude / 32768.0));
}

inline double vibratoDepthCents(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (version == AKAOSNES_V1) {
    return v1VibratoDepthCentsForHighByte(v1VibratoHighByteAmplitude(rate, depth));
  }

  if (version == AKAOSNES_V2) {
    return v2VibratoDepthCents(rate, depth);
  }

  return vibratoDepthCentsForAmplitude(modulationAmplitude(version, rate, depth));
}

inline double tremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  const double troughScale = std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0));
  return -20.0 * std::log10(troughScale);
}

inline double v1TremoloDepthDb(uint8_t rate, uint8_t depth) {
  const uint8_t counter = v1RateCounter(rate);
  if (counter == 0 || depth == 0) {
    return 0.0;
  }

  const uint32_t quotient = (256u * depth) / counter;
  if (quotient == 0) {
    return 0.0;
  }

  uint16_t accumulator = 0;
  uint16_t step = static_cast<uint16_t>((0x10000u - (quotient & 0xffffu)) & 0xffffu);
  uint8_t countdown = counter;
  uint8_t minScaleByte = 0xff;
  bool foundAttenuatedSample = false;

  for (uint16_t frame = 0; frame < static_cast<uint16_t>(2 * (counter + 1)); frame++) {
    const uint16_t oldStep = step;
    if (countdown == 0) {
      step = static_cast<uint16_t>((0x10000u - step) & 0xffffu);
      countdown = counter;
    }
    else {
      countdown--;
    }

    accumulator = static_cast<uint16_t>(accumulator + oldStep);
    const uint8_t scaleByte = static_cast<uint8_t>(accumulator >> 8);
    if (scaleByte != 0) {
      minScaleByte = std::min(minScaleByte, scaleByte);
      foundAttenuatedSample = true;
    }
  }

  if (!foundAttenuatedSample) {
    return 0.0;
  }

  return -20.0 * std::log10(minScaleByte / 256.0);
}

inline double v2TremoloDepthDb(uint8_t rate, uint8_t depth) {
  const double amplitude = v2PhaseHighByteAmplitude(rate, depth);
  if (amplitude <= 0.0) {
    return 0.0;
  }

  if (depth >= 0x40 && depth < 0x80) {
    const double boostScale = 1.0 + (std::min(127.0, amplitude) / 256.0);
    return 20.0 * std::log10(boostScale);
  }

  const double troughScale = (256.0 - std::min(128.0, amplitude)) / 256.0;
  return -20.0 * std::log10(troughScale);
}

inline double tremoloDepthDb(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (version == AKAOSNES_V1) {
    return v1TremoloDepthDb(rate, depth);
  }

  if (version == AKAOSNES_V2) {
    return v2TremoloDepthDb(rate, depth);
  }

  return tremoloDepthDbForAmplitude(modulationAmplitude(version, rate, depth));
}

inline constexpr uint8_t delayTicks(AkaoSnesVersion version, uint8_t delay) {
  if (version == AKAOSNES_V1) {
    return (delay == 0xff) ? 0 : delay;
  }

  return delay;
}

inline double delaySeconds(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  const uint8_t ticks = delayTicks(version, delay);
  if (ticks == 0) {
    return 0.0;
  }

  const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
  return ticks * (256.0 / (frameRateHz(timer0Frequency) * safeTempo));
}

inline constexpr double kMaxV1DelaySeconds = 254.0 * 256.0 / (8000.0 / kMinTimer0Frequency);
inline constexpr double kMaxDelaySeconds = 255.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);
inline const double kMaxV1VibratoDepthCents = v1VibratoDepthCentsForHighByte(255);
inline const double kMaxV2VibratoDepthCents = 1200.0 * std::log2(1.0 + (15.0 * 127.0 / 32768.0));
inline const double kMaxV3VibratoDepthCents = vibratoDepthCentsForAmplitude(127.0);
inline const double kMaxV4VibratoDepthCents = vibratoDepthCentsForAmplitude(64.0);
inline const double kMaxV1TremoloDepthDb = -20.0 * std::log10(1.0 / 256.0);
inline const double kMaxV2TremoloDepthDb = -20.0 * std::log10(0.5);
inline const double kMaxV3TremoloDepthDb = tremoloDepthDbForAmplitude(127.0);
inline const double kMaxV4TremoloDepthDb = tremoloDepthDbForAmplitude(64.0);

inline double maxVibratoDepthCents(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return kMaxV1VibratoDepthCents;
  }

  if (version == AKAOSNES_V2) {
    return kMaxV2VibratoDepthCents;
  }

  return (version == AKAOSNES_V3) ? kMaxV3VibratoDepthCents : kMaxV4VibratoDepthCents;
}

inline double maxTremoloDepthDb(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return kMaxV1TremoloDepthDb;
  }

  if (version == AKAOSNES_V2) {
    return kMaxV2TremoloDepthDb;
  }

  return (version == AKAOSNES_V3) ? kMaxV3TremoloDepthDb : kMaxV4TremoloDepthDb;
}

inline double maxDelaySeconds(AkaoSnesVersion version) {
  return (version == AKAOSNES_V1) ? kMaxV1DelaySeconds : kMaxDelaySeconds;
}

inline VibratoModulationSpec vibratoSpec(AkaoSnesVersion version) {
  return {
      maxVibratoDepthCents(version),
      minLfoRateHz(version),
      maxLfoRateHz(version),
      DelayRange {
          0.0,
          maxDelaySeconds(version),
      },
  };
}

inline TremoloModulationSpec tremoloSpec(AkaoSnesVersion version) {
  return {
      maxTremoloDepthDb(version),
      minLfoRateHz(version),
      maxLfoRateHz(version),
      version == AKAOSNES_V1 ? TremoloGainMode::NoBoost : TremoloGainMode::BipolarAroundNominal,
      DelayRange {
          0.0,
          maxDelaySeconds(version),
      },
      ModSource::Effects4Depth,
      ModSource::Effects2Depth,
      ModSource::Effects5Depth,
  };
}

inline uint8_t vibratoDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (!isLfoActive(version, rate, depth)) {
    return 0;
  }

  const int midiValue =
      static_cast<int>(std::lround(128.0 * vibratoDepthCents(version, rate, depth) /
                                   maxVibratoDepthCents(version)));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

inline uint8_t tremoloDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (!isLfoActive(version, rate, depth)) {
    return 0;
  }

  const int midiValue =
      static_cast<int>(std::lround(128.0 * tremoloDepthDb(version, rate, depth) /
                                   maxTremoloDepthDb(version)));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

inline uint8_t rateMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency) {
  return midiValueForHertzInRange(lfoRateHz(version, rate, depth, timer0Frequency),
                                  minLfoRateHz(version),
                                  maxLfoRateHz(version));
}

inline uint8_t delayMidiValue(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  return midiValueForSecondsInRange(delaySeconds(version, delay, tempo, timer0Frequency),
                                    0.0,
                                    maxDelaySeconds(version));
}

inline uint32_t v1VibratoRampTicks(uint8_t rate, uint8_t tempo) {
  const uint8_t counter = v1RateCounter(rate);
  if (counter == 0) {
    return 0;
  }

  const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
  const double rampFrames = 14.0 * (counter + 1);
  return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(rampFrames * safeTempo / 256.0)));
}

}  // namespace akao_snes::modulation

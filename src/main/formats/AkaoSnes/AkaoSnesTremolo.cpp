/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesModulation.h"
#include <algorithm>
#include <cmath>

namespace akao_snes::modulation::detail {
// Shared helpers live in AkaoSnesModulation.cpp; keep them out of the public header for now.
uint8_t v1RateCounter(uint8_t rate);
uint8_t modulationMagnitude(AkaoSnesVersion version, uint8_t depth);
double minLfoRateHz(AkaoSnesVersion version);
double maxLfoRateHz(AkaoSnesVersion version);
double maxDelaySeconds(AkaoSnesVersion version);
double v2PhaseHighByteAmplitude(uint8_t rate, uint8_t depth);
double v4PhaseHighByteAmplitude(uint8_t rate, uint8_t depth);
double modulationAmplitude(AkaoSnesVersion version, uint8_t rate, uint8_t depth);
}  // namespace akao_snes::modulation::detail

namespace akao_snes::modulation {
namespace {

constexpr double kV3SteppedTremoloSmoothLfoCompensation = 2.0;

double tremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  const double troughScale = std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0));
  return -20.0 * std::log10(troughScale);
}

double v1TremoloDepthDb(uint8_t rate, uint8_t depth) {
  const uint8_t counter = detail::v1RateCounter(rate);
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

  // FF4 tremolo is not a simple symmetric amplitude. Simulate one full driver LFO cycle and export the deepest attenuation.
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

double v2TremoloDepthDb(uint8_t rate, uint8_t depth) {
  const double amplitude = detail::v2PhaseHighByteAmplitude(rate, depth);
  if (amplitude <= 0.0) {
    return 0.0;
  }

  if (depth >= 0x40 && depth < 0x80) {
    // This V2 depth range boosts above nominal instead of attenuating below it.
    const double boostScale = 1.0 + (std::min(127.0, amplitude) / 256.0);
    return 20.0 * std::log10(boostScale);
  }

  const double troughScale = (256.0 - std::min(128.0, amplitude)) / 256.0;
  return -20.0 * std::log10(troughScale);
}

double v3TremoloPeakToTroughDb(uint8_t depth) {
  if (depth == 0) {
    return 0.0;
  }

  const double magnitude = detail::modulationMagnitude(AKAOSNES_V3, depth);
  // SF2/DLS cannot reproduce V3's signed stepped tremolo shapes. Preserve the
  // control byte's magnitude as attenuation-equivalent depth for every shape.
  const double troughScale = (128.0 - std::min(127.0, magnitude)) / 128.0;
  return -20.0 * std::log10(troughScale);
}

double tremoloDepthDb(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (version == AKAOSNES_V1) {
    return v1TremoloDepthDb(rate, depth);
  }

  if (version == AKAOSNES_V2) {
    return v2TremoloDepthDb(rate, depth);
  }

  return tremoloDepthDbForAmplitude(detail::modulationAmplitude(version, rate, depth));
}

uint8_t midiValueForDepthRange(double value, double maxValue) {
  if (maxValue <= 0.0) {
    return 0;
  }

  const int midiValue = static_cast<int>(std::lround(128.0 * value / maxValue));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

const double kMaxV1TremoloDepthDb = -20.0 * std::log10(1.0 / 256.0);
const double kMaxV2TremoloDepthDb = -20.0 * std::log10(0.5);
const double kMaxV3TremoloDepthDb = tremoloDepthDbForAmplitude(127.0);
const double kMaxV4TremoloDepthDb = tremoloDepthDbForAmplitude(64.0);

double maxTremoloDepthDb(AkaoSnesVersion version) {
  switch (version) {
    case AKAOSNES_V1:
      return kMaxV1TremoloDepthDb;
    case AKAOSNES_V2:
      return kMaxV2TremoloDepthDb;
    case AKAOSNES_V3:
      return kMaxV3TremoloDepthDb;
    case AKAOSNES_V4:
    default:
      return kMaxV4TremoloDepthDb;
  }
}

}  // namespace

TremoloModulationSpec tremoloSpec(AkaoSnesVersion version) {
  return {
      maxTremoloDepthDb(version),
      detail::minLfoRateHz(version),
      detail::maxLfoRateHz(version),
      version == AKAOSNES_V1 ? TremoloGainMode::NoBoost : TremoloGainMode::BipolarAroundNominal,
      DelayRange {
          0.0,
          detail::maxDelaySeconds(version),
      },
  };
}

uint8_t tremoloDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t delay) {
  if (!isLfoActive(version, rate, depth)) {
    return 0;
  }

  double depthDb = 0.0;
  if (version == AKAOSNES_V3) {
    depthDb = kV3SteppedTremoloSmoothLfoCompensation * v3TremoloPeakToTroughDb(depth);
  }
  else if (version == AKAOSNES_V4 && delay != 0) {
    // V4 delayed tremolo runs at one-quarter step and does not ramp to full depth.
    depthDb = tremoloDepthDbForAmplitude(detail::v4PhaseHighByteAmplitude(rate, depth) / 4.0);
  }
  else {
    depthDb = tremoloDepthDb(version, rate, depth);
  }

  return midiValueForDepthRange(depthDb, maxTremoloDepthDb(version));
}

}  // namespace akao_snes::modulation

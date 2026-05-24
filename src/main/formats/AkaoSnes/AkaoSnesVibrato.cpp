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
uint16_t effectiveRateFrames(AkaoSnesVersion version, uint8_t rate, uint8_t depth);
double minLfoRateHz(AkaoSnesVersion version);
double maxLfoRateHz(AkaoSnesVersion version);
double maxDelaySeconds(AkaoSnesVersion version);
double v2PhaseHighByteAmplitude(uint8_t rate, uint8_t depth);
double modulationAmplitude(AkaoSnesVersion version, uint8_t rate, uint8_t depth);
uint32_t driverFramesToTicks(double frames, uint8_t tempo);
}  // namespace akao_snes::modulation::detail

namespace akao_snes::modulation {
namespace {

uint8_t v1VibratoHighByteAmplitude(uint8_t rate, uint8_t depth) {
  const uint8_t counter = detail::v1RateCounter(rate);
  if (counter == 0 || depth == 0) {
    return 0;
  }

  // V1 derives amplitude through an 8-bit fractional step; keep the driver's truncation.
  const uint32_t step = (256u * depth) / counter;
  return static_cast<uint8_t>((step * counter) / 256u);
}

double vibratoDepthCentsForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  // Later drivers apply amplitude as a pitch ratio delta; convert both sides and keep the larger bend.
  const double ratio = 15.0 * amplitude / 32768.0;
  const double centsUp = 1200.0 * std::log2(1.0 + ratio);
  const double centsDown = -1200.0 * std::log2(1.0 - ratio);
  return std::max(centsUp, centsDown);
}

double v1VibratoDepthCentsForHighByte(uint8_t amplitude) {
  if (amplitude == 0) {
    return 0.0;
  }

  // FF4/V1 uses its own pitch scale denominator instead of the later 15/32768 ratio.
  return 1200.0 * std::log2(1.0 + (amplitude / 3072.0));
}

double v2VibratoDepthCents(uint8_t rate, uint8_t depth) {
  const double amplitude = std::min(127.0, detail::v2PhaseHighByteAmplitude(rate, depth));
  if (amplitude <= 0.0) {
    return 0.0;
  }

  if (depth < 0x40) {
    // Low-depth V2 vibrato is downward-biased in the driver pitch formula.
    return -1200.0 * std::log2(1.0 - (15.0 * amplitude / 65536.0));
  }

  // Depth bit 6 selects the upward-side scaling.
  return 1200.0 * std::log2(1.0 + (15.0 * amplitude / 32768.0));
}

double vibratoDepthCents(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  // Reproduce each driver's pitch math first, then report the result as SF2/DLS cents.
  if (version == AKAOSNES_V1) {
    return v1VibratoDepthCentsForHighByte(v1VibratoHighByteAmplitude(rate, depth));
  }

  if (version == AKAOSNES_V2) {
    return v2VibratoDepthCents(rate, depth);
  }

  return vibratoDepthCentsForAmplitude(detail::modulationAmplitude(version, rate, depth));
}

const double kMaxV1VibratoDepthCents = v1VibratoDepthCentsForHighByte(255);
const double kMaxV2VibratoDepthCents = 1200.0 * std::log2(1.0 + (15.0 * 127.0 / 32768.0));
const double kMaxV3VibratoDepthCents = vibratoDepthCentsForAmplitude(127.0);
const double kMaxV4VibratoDepthCents = vibratoDepthCentsForAmplitude(64.0);

double maxVibratoDepthCents(AkaoSnesVersion version) {
  switch (version) {
    case AKAOSNES_V1:
      return kMaxV1VibratoDepthCents;
    case AKAOSNES_V2:
      return kMaxV2VibratoDepthCents;
    case AKAOSNES_V3:
      return kMaxV3VibratoDepthCents;
    case AKAOSNES_V4:
    default:
      return kMaxV4VibratoDepthCents;
  }
}

}  // namespace

VibratoModulationSpec vibratoSpec(AkaoSnesVersion version) {
  // Instrument-level SF2/DLS setup: generators define minima, controllers sweep to these maxima.
  return {
      maxVibratoDepthCents(version),
      detail::minLfoRateHz(version),
      detail::maxLfoRateHz(version),
      DelayRange {
          0.0,
          detail::maxDelaySeconds(version),
      },
  };
}

uint8_t vibratoDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (!isLfoActive(version, rate, depth)) {
    return 0;
  }

  // The configured vibrato depth source is normalized against the largest depth this version can produce.
  const int midiValue =
      static_cast<int>(std::lround(128.0 * vibratoDepthCents(version, rate, depth) /
                                   maxVibratoDepthCents(version)));
  return static_cast<uint8_t>(std::clamp(midiValue, version == AKAOSNES_V3 ? 1 : 0, 127));
}

uint32_t v1VibratoRampTicks(uint8_t rate, uint8_t tempo) {
  const uint8_t counter = detail::v1RateCounter(rate);
  if (counter == 0) {
    return 0;
  }

  // FF4 derives the fade length from the vibrato rate counter in driver frames,
  // then the sequencer tempo determines how many music ticks those frames span.
  const double rampFrames = 14.0 * (counter + 1);
  return detail::driverFramesToTicks(rampFrames, tempo);
}

uint32_t v4VibratoRampTicks(uint8_t rate, uint8_t tempo) {
  // V4 starts delayed vibrato at quarter slope and reaches full depth after
  // seven rate intervals. Approximate that slope ramp as one smooth depth fade.
  const double rampFrames = 7.0 * detail::effectiveRateFrames(AKAOSNES_V4, rate, 0);
  return detail::driverFramesToTicks(rampFrames, tempo);
}

}  // namespace akao_snes::modulation

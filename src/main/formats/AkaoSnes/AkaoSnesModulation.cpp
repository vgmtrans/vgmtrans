/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesModulation.h"
#include <algorithm>
#include <cmath>

namespace akao_snes::modulation {
namespace {

/*
 * These helpers translate the SNES driver's vibrato state into MIDI LFO
 * parameters. VGMTrans exports those through the standard modulation metadata
 * plus controller automation.
 *
 * Version summary:
 * - V1: rate uses the upper 7 bits (rate >> 1), depth is literal, and
 *   vibrato has a reusable per-note fade-in.
 * - V2: rate uses the lower 7 bits, depth bit 7 halves the rate, and
 *   depth bits also encode pitch scaling behavior.
 * - V3: rate is stored as rate + 1 and depth is a 6-bit magnitude scaled
 *   to an odd range.
 * - V4: rate zero means 256 frames; depth is a 6-bit magnitude.
 */

constexpr uint8_t kMinTimer0Frequency = 0x24;
constexpr uint8_t kMaxTimer0Frequency = 0x2a;

constexpr uint8_t v1RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate >> 1);
}

constexpr uint8_t v2RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate & 0x7f);
}

constexpr uint16_t effectiveRateFrames(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  // All versions produce a half-cycle over this many driver frames. The frame
  // rate itself is timer0-dependent and is converted to MIDI Hz below.
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

constexpr uint8_t modulationMagnitude(AkaoSnesVersion version, uint8_t depth) {
  // Convert version-specific depth encoding to the driver's high-byte LFO
  // amplitude space before mapping it to cents.
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

double frameRateHz(uint8_t timer0Frequency) {
  return 8000.0 / timer0Frequency;
}

double lfoRateHz(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency) {
  const uint16_t frames = effectiveRateFrames(version, rate, depth);
  return (frames == 0) ? 0.0 : frameRateHz(timer0Frequency) / (2.0 * frames);
}

double minLfoRateHz(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 128.0);
  }

  if (version == AKAOSNES_V2) {
    return frameRateHz(kMinTimer0Frequency) / (4.0 * 127.0);
  }

  return 8000.0 / kMaxTimer0Frequency / (2.0 * 256.0);
}

double maxLfoRateHz(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 2.0);
  }

  if (version == AKAOSNES_V2) {
    return frameRateHz(kMinTimer0Frequency) / 2.0;
  }

  return 8000.0 / kMinTimer0Frequency / 2.0;
}

uint16_t v4LfoStep(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  const uint16_t frames = effectiveRateFrames(AKAOSNES_V4, rate, depth);
  const uint8_t magnitude = modulationMagnitude(AKAOSNES_V4, depth);
  const uint16_t step = static_cast<uint16_t>(64 * magnitude / frames);
  return static_cast<uint16_t>(4 * std::max<uint16_t>(1, step));
}

double v4PhaseHighByteAmplitude(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0.0;
  }

  return v4LfoStep(rate, depth) * effectiveRateFrames(AKAOSNES_V4, rate, depth) / 256.0;
}

double v2PhaseHighByteAmplitude(uint8_t rate, uint8_t depth) {
  const uint8_t frames = v2RateCounter(rate);
  if (frames == 0) {
    return 0.0;
  }

  // V2 computes a phase step from depth and rate. The high byte of the phase
  // accumulator is what ultimately becomes pitch modulation magnitude.
  const uint16_t step = static_cast<uint16_t>(512 * modulationMagnitude(AKAOSNES_V2, depth) / frames);
  return std::min(128.0, static_cast<double>((step * frames) >> 8));
}

double modulationAmplitude(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
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

uint8_t v1VibratoHighByteAmplitude(uint8_t rate, uint8_t depth) {
  const uint8_t counter = v1RateCounter(rate);
  if (counter == 0 || depth == 0) {
    return 0;
  }

  const uint32_t step = (256u * depth) / counter;
  return static_cast<uint8_t>((step * counter) / 256u);
}

double vibratoDepthCentsForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  const double ratio = 15.0 * amplitude / 32768.0;
  const double centsUp = 1200.0 * std::log2(1.0 + ratio);
  const double centsDown = -1200.0 * std::log2(1.0 - ratio);
  return std::max(centsUp, centsDown);
}

double v1VibratoDepthCentsForHighByte(uint8_t amplitude) {
  if (amplitude == 0) {
    return 0.0;
  }

  return 1200.0 * std::log2(1.0 + (amplitude / 3072.0));
}

double v2VibratoDepthCents(uint8_t rate, uint8_t depth) {
  const double amplitude = std::min(127.0, v2PhaseHighByteAmplitude(rate, depth));
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
  if (version == AKAOSNES_V1) {
    return v1VibratoDepthCentsForHighByte(v1VibratoHighByteAmplitude(rate, depth));
  }

  if (version == AKAOSNES_V2) {
    return v2VibratoDepthCents(rate, depth);
  }

  return vibratoDepthCentsForAmplitude(modulationAmplitude(version, rate, depth));
}

double delaySeconds(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  const uint8_t ticks = delayTicks(version, delay);
  if (ticks == 0) {
    return 0.0;
  }

  const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
  return ticks * (256.0 / (frameRateHz(timer0Frequency) * safeTempo));
}

constexpr double kMaxV1DelaySeconds = 254.0 * 256.0 / (8000.0 / kMinTimer0Frequency);
constexpr double kMaxDelaySeconds = 255.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);
const double kMaxV1VibratoDepthCents = v1VibratoDepthCentsForHighByte(255);
const double kMaxV2VibratoDepthCents = 1200.0 * std::log2(1.0 + (15.0 * 127.0 / 32768.0));
const double kMaxV3VibratoDepthCents = vibratoDepthCentsForAmplitude(127.0);
const double kMaxV4VibratoDepthCents = vibratoDepthCentsForAmplitude(64.0);

double maxVibratoDepthCents(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return kMaxV1VibratoDepthCents;
  }

  if (version == AKAOSNES_V2) {
    return kMaxV2VibratoDepthCents;
  }

  return (version == AKAOSNES_V3) ? kMaxV3VibratoDepthCents : kMaxV4VibratoDepthCents;
}

double maxDelaySeconds(AkaoSnesVersion version) {
  return (version == AKAOSNES_V1) ? kMaxV1DelaySeconds : kMaxDelaySeconds;
}

}  // namespace

bool supportsLfoAutomation(AkaoSnesVersion version) {
  return version == AKAOSNES_V1 || version == AKAOSNES_V2 ||
         version == AKAOSNES_V3 || version == AKAOSNES_V4;
}

bool isLfoActive(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (!supportsLfoAutomation(version)) {
    return false;
  }

  if (version == AKAOSNES_V2) {
    // V2 can be active with a zero depth byte because the low six bits are
    // interpreted as magnitude + 1; only a zero rate counter disables it.
    return v2RateCounter(rate) != 0;
  }

  if (depth == 0) {
    return false;
  }

  return version != AKAOSNES_V1 || v1RateCounter(rate) != 0;
}

uint8_t delayTicks(AkaoSnesVersion version, uint8_t delay) {
  if (version == AKAOSNES_V1) {
    // FF4 stores delay + 1 in the driver; a literal $ff wraps to zero.
    return (delay == 0xff) ? 0 : delay;
  }

  return delay;
}

VibratoModulationSpec vibratoSpec(AkaoSnesVersion version) {
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

uint8_t vibratoDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (!isLfoActive(version, rate, depth)) {
    return 0;
  }

  const int midiValue =
      static_cast<int>(std::lround(128.0 * vibratoDepthCents(version, rate, depth) /
                                   maxVibratoDepthCents(version)));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

uint8_t rateMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency) {
  return midiValueForHertzInRange(lfoRateHz(version, rate, depth, timer0Frequency),
                                  minLfoRateHz(version),
                                  maxLfoRateHz(version));
}

uint8_t delayMidiValue(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  return midiValueForSecondsInRange(delaySeconds(version, delay, tempo, timer0Frequency),
                                    0.0,
                                    maxDelaySeconds(version));
}

uint32_t v1VibratoRampTicks(uint8_t rate, uint8_t tempo) {
  const uint8_t counter = v1RateCounter(rate);
  if (counter == 0) {
    return 0;
  }

  const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
  // FF4 derives the fade length from the vibrato rate counter in driver frames,
  // then the sequencer tempo determines how many music ticks those frames span.
  const double rampFrames = 14.0 * (counter + 1);
  return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(rampFrames * safeTempo / 256.0)));
}

}  // namespace akao_snes::modulation

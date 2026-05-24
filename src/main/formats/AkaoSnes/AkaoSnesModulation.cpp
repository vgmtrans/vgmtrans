/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesModulation.h"
#include <algorithm>
#include <cmath>

namespace akao_snes::modulation::detail {

/*
 * These helpers translate the SNES driver's LFO state into SF2/DLS LFO ranges
 * plus the MIDI controller values that drive them. The driver writes vibrato as a
 * pitch offset and tremolo as a volume-scale offset.
 *
 * Version summary:
 * - V1: rate uses the upper 7 bits (rate >> 1), depth is literal, and
 *   vibrato has a reusable per-note fade-in.
 * - V2: rate uses the lower 7 bits, depth bit 7 halves the rate, and depth bits
 *   also encode direction/boost behavior.
 * - V3: rate is stored as rate + 1 and depth is a 6-bit magnitude scaled
 *   to an odd range.
 * - V4: rate zero means 256 frames; depth is a 6-bit magnitude.
 */

// These are timer0 latch bytes, not Hertz values: driver frames run at 8000 / latch.
constexpr uint8_t kMinTimer0Frequency = 0x24;
constexpr uint8_t kMaxTimer0Frequency = 0x2a;

uint8_t v1RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate >> 1);
}

uint8_t v2RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate & 0x7f);
}

uint16_t effectiveRateFrames(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  // All versions produce one half-cycle over this many timer0 frames; lfoRateHz() turns that into cycles/sec.
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

uint8_t modulationMagnitude(AkaoSnesVersion version, uint8_t depth) {
  // Convert version-specific depth bytes to the driver's high-byte LFO amplitude space.
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
  // SPC timer0 runs from an 8 kHz source divided by the latch byte.
  return 8000.0 / timer0Frequency;
}

double lfoRateHz(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency) {
  // SF2/DLS vibrato LFO frequency is full triangle cycles/sec, and one full cycle is two ramps.
  const uint16_t frames = effectiveRateFrames(version, rate, depth);
  return (frames == 0) ? 0.0 : frameRateHz(timer0Frequency) / (2.0 * frames);
}

double minLfoRateHz(AkaoSnesVersion version) {
  // TODO: use the more precise minimum rates when we migrate off of BASS MIDI
  return 1.0 / 16.0;

  // Lower bound for controller scaling: slowest active half-cycle, using the
  // slowest timer0 latch when that version family has multiple latches.
  if (version == AKAOSNES_V1) {
    // V1 always uses timer0 latch $24, and active counters map to 2..128 frames.
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 128.0);
  }

  if (version == AKAOSNES_V2) {
    // V2 always uses timer0 latch $24. Depth bit 7 doubles the 1..127 frame counter
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 2.0 * 127.0);
  }

  if (version == AKAOSNES_V3) {
    // V3 always uses timer0 latch $24.
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 256.0);
  }

  // V4 reaches 256-frame half-cycles and some titles use timer0 latches up to $2a.
  return 8000.0 / kMaxTimer0Frequency / (2.0 * 256.0);
}

double maxLfoRateHz(AkaoSnesVersion version) {
  // Upper bound for controller scaling: fastest active half-cycle at the fastest timer0 latch.
  if (version == AKAOSNES_V1) {
    // V1's rate counter is stored as rate >> 1, then incremented; zero disables
    // the LFO, so the fastest active half-cycle is 2 frames.
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 2.0);
  }

  if (version == AKAOSNES_V2) {
    // V2's active counter can be 1 frame, provided depth bit 7 is clear.
    return frameRateHz(kMinTimer0Frequency) / 2.0;
  }

  // V3 has rate 0 => 1 frame; V4 reserves rate 0 for 256 frames, but rate 1 is
  // still a 1-frame half-cycle. Both use $24 as the fastest timer0 latch.
  return 8000.0 / kMinTimer0Frequency / 2.0;
}

uint16_t v4LfoStep(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0;
  }

  // V4 advances a phase accumulator by a quantized step and enforces a minimum nonzero step.
  const uint16_t frames = effectiveRateFrames(AKAOSNES_V4, rate, depth);
  const uint8_t magnitude = modulationMagnitude(AKAOSNES_V4, depth);
  const uint16_t step = static_cast<uint16_t>(64 * magnitude / frames);
  return static_cast<uint16_t>(4 * std::max<uint16_t>(1, step));
}

double v4PhaseHighByteAmplitude(uint8_t rate, uint8_t depth) {
  if (depth == 0) {
    return 0.0;
  }

  // Over one ramp, the high byte reached by the phase accumulator is the pitch amplitude.
  return v4LfoStep(rate, depth) * effectiveRateFrames(AKAOSNES_V4, rate, depth) / 256.0;
}

double v2PhaseHighByteAmplitude(uint8_t rate, uint8_t depth) {
  const uint8_t frames = v2RateCounter(rate);
  if (frames == 0) {
    return 0.0;
  }

  // V2 computes a phase step from depth and rate. The accumulator high byte becomes pitch/volume magnitude.
  const uint16_t step = static_cast<uint16_t>(512 * modulationMagnitude(AKAOSNES_V2, depth) / frames);
  return std::min(128.0, static_cast<double>((step * frames) >> 8));
}

double modulationAmplitude(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  // Normalize every version to the high-byte amplitude expected by the cents conversion.
  if (version == AKAOSNES_V2) {
    return v2PhaseHighByteAmplitude(rate, depth);
  }

  if (depth == 0) {
    return 0.0;
  }

  if (version == AKAOSNES_V3) {
    const double magnitude = modulationMagnitude(version, depth);
    // One-sided V3 modes alternate between 0 and +/-m. A centered SF2 LFO cannot
    // reproduce the DC offset, so approximate the pitch movement with half depth.
    return ((depth & 0xc0) == 0xc0) ? magnitude : magnitude / 2.0;
  }

  return v4PhaseHighByteAmplitude(rate, depth);
}

double delaySeconds(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  const uint8_t ticks = delayTicks(version, delay);
  if (ticks == 0) {
    return 0.0;
  }

  const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
  // Delay ticks are tempo-scaled music ticks; one tick is 256 / (driverFramesPerSecond * tempo).
  return ticks * (256.0 / (frameRateHz(timer0Frequency) * safeTempo));
}

uint32_t driverFramesToTicks(double frames, uint8_t tempo) {
  // Tempo 0 stops music ticks; export automation still needs finite timing, so use the slowest nonzero tempo.
  const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
  return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(frames * safeTempo / 256.0)));
}

constexpr double kMaxV1DelaySeconds = 254.0 * 256.0 / (8000.0 / kMinTimer0Frequency);
constexpr double kMaxV4DelaySeconds = 254.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);
constexpr double kMaxDelaySeconds = 255.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);

double maxDelaySeconds(AkaoSnesVersion version) {
  // Export range ceiling for the delay controller; V1 wraps $ff and V4 decrements
  // the active delay during the setup tick, so neither uses the full 255 ticks.
  if (version == AKAOSNES_V1) {
    return kMaxV1DelaySeconds;
  }

  return (version == AKAOSNES_V4) ? kMaxV4DelaySeconds : kMaxDelaySeconds;
}

}  // namespace akao_snes::modulation::detail

namespace akao_snes::modulation {

bool isLfoActive(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
  if (version == AKAOSNES_V2) {
    // V2 can be active with a zero depth byte because the low six bits are
    // interpreted as magnitude + 1; only a zero rate counter disables it.
    return detail::v2RateCounter(rate) != 0;
  }

  if (depth == 0) {
    return false;
  }

  return version != AKAOSNES_V1 || detail::v1RateCounter(rate) != 0;
}

uint8_t delayTicks(AkaoSnesVersion version, uint8_t delay) {
  if (version == AKAOSNES_V1) {
    // FF4 stores delay + 1 in the driver; a literal $ff wraps to zero.
    return (delay == 0xff) ? 0 : delay;
  }

  if (version == AKAOSNES_V4) {
    // FF6-family drivers decrement delay during the same sequencer pass that
    // initializes vibrato, so $01 enables fade-in with no audible pre-delay.
    return (delay == 0) ? 0 : static_cast<uint8_t>(delay - 1);
  }

  return delay;
}

uint8_t rateMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency) {
  return midiValueForHertzInRange(detail::lfoRateHz(version, rate, depth, timer0Frequency),
                                  detail::minLfoRateHz(version),
                                  detail::maxLfoRateHz(version));
}

uint8_t delayMidiValue(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  // The configured vibrato delay source drives LFO delay within the version's exported seconds range.
  return midiValueForSecondsInRange(detail::delaySeconds(version, delay, tempo, timer0Frequency),
                                    0.0,
                                    detail::maxDelaySeconds(version));
}

uint32_t v3LfoRampTicks(uint8_t rate, uint8_t tempo) {
  // V3's delayed note-start ramp reaches full depth after six LFO update
  // intervals; export that as one smooth SF2/DLS depth fade.
  const double rampFrames = 6.0 * detail::effectiveRateFrames(AKAOSNES_V3, rate, 0);
  return detail::driverFramesToTicks(rampFrames, tempo);
}

}  // namespace akao_snes::modulation

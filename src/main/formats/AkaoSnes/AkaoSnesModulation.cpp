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
// V3 holds each tremolo value for a full rate interval, but SF2/DLS exports it
// through a smooth synth LFO. This compensates for that weaker approximation.
constexpr double kV3SteppedTremoloSmoothLfoCompensation = 2.0;

constexpr uint8_t v1RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate >> 1);
}

constexpr uint8_t v2RateCounter(uint8_t rate) {
  return static_cast<uint8_t>(rate & 0x7f);
}

constexpr uint16_t effectiveRateFrames(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
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

constexpr uint8_t modulationMagnitude(AkaoSnesVersion version, uint8_t depth) {
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

uint8_t v1VibratoHighByteAmplitude(uint8_t rate, uint8_t depth) {
  const uint8_t counter = v1RateCounter(rate);
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
  // Reproduce each driver's pitch math first, then report the result as SF2/DLS cents.
  if (version == AKAOSNES_V1) {
    return v1VibratoDepthCentsForHighByte(v1VibratoHighByteAmplitude(rate, depth));
  }

  if (version == AKAOSNES_V2) {
    return v2VibratoDepthCents(rate, depth);
  }

  return vibratoDepthCentsForAmplitude(modulationAmplitude(version, rate, depth));
}

double tremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  const double troughScale = std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0));
  return -20.0 * std::log10(troughScale);
}

double v1TremoloDepthDb(uint8_t rate, uint8_t depth) {
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
  const double amplitude = v2PhaseHighByteAmplitude(rate, depth);
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

uint8_t midiValueForDepthRange(double value, double maxValue) {
  if (maxValue <= 0.0) {
    return 0;
  }

  const int midiValue = static_cast<int>(std::lround(128.0 * value / maxValue));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

double v3TremoloPeakToTroughDb(uint8_t depth) {
  if (depth == 0) {
    return 0.0;
  }

  const double magnitude = modulationMagnitude(AKAOSNES_V3, depth);
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

  return tremoloDepthDbForAmplitude(modulationAmplitude(version, rate, depth));
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
const double kMaxV1VibratoDepthCents = v1VibratoDepthCentsForHighByte(255);
const double kMaxV2VibratoDepthCents = 1200.0 * std::log2(1.0 + (15.0 * 127.0 / 32768.0));
const double kMaxV3VibratoDepthCents = vibratoDepthCentsForAmplitude(127.0);
const double kMaxV4VibratoDepthCents = vibratoDepthCentsForAmplitude(64.0);
const double kMaxV1TremoloDepthDb = -20.0 * std::log10(1.0 / 256.0);
const double kMaxV2TremoloDepthDb = -20.0 * std::log10(0.5);
const double kMaxV3TremoloDepthDb = tremoloDepthDbForAmplitude(127.0);
const double kMaxV4TremoloDepthDb = tremoloDepthDbForAmplitude(64.0);

double maxVibratoDepthCents(AkaoSnesVersion version) {
  // Export range ceiling for the ModWheel-to-vibrato-depth mapping.
  if (version == AKAOSNES_V1) {
    return kMaxV1VibratoDepthCents;
  }

  if (version == AKAOSNES_V2) {
    return kMaxV2VibratoDepthCents;
  }

  return (version == AKAOSNES_V3) ? kMaxV3VibratoDepthCents : kMaxV4VibratoDepthCents;
}

double maxTremoloDepthDb(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return kMaxV1TremoloDepthDb;
  }

  if (version == AKAOSNES_V2) {
    return kMaxV2TremoloDepthDb;
  }

  return (version == AKAOSNES_V3) ? kMaxV3TremoloDepthDb : kMaxV4TremoloDepthDb;
}

double maxDelaySeconds(AkaoSnesVersion version) {
  // Export range ceiling for the delay controller; V1 wraps $ff and V4 decrements
  // the active delay during the setup tick, so neither uses the full 255 ticks.
  if (version == AKAOSNES_V1) {
    return kMaxV1DelaySeconds;
  }

  return (version == AKAOSNES_V4) ? kMaxV4DelaySeconds : kMaxDelaySeconds;
}

}  // namespace

bool isLfoActive(AkaoSnesVersion version, uint8_t rate, uint8_t depth) {
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

  if (version == AKAOSNES_V4) {
    // FF6-family drivers decrement delay during the same sequencer pass that
    // initializes vibrato, so $01 enables fade-in with no audible pre-delay.
    return (delay == 0) ? 0 : static_cast<uint8_t>(delay - 1);
  }

  return delay;
}

VibratoModulationSpec vibratoSpec(AkaoSnesVersion version) {
  // Instrument-level SF2/DLS setup: generators define minima, controllers sweep to these maxima.
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

TremoloModulationSpec tremoloSpec(AkaoSnesVersion version) {
  return {
      maxTremoloDepthDb(version),
      minLfoRateHz(version),
      maxLfoRateHz(version),
      version == AKAOSNES_V1 ? TremoloGainMode::NoBoost : TremoloGainMode::BipolarAroundNominal,
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

  // The configured vibrato depth source is normalized against the largest depth this version can produce.
  const int midiValue =
      static_cast<int>(std::lround(128.0 * vibratoDepthCents(version, rate, depth) /
                                   maxVibratoDepthCents(version)));
  return static_cast<uint8_t>(std::clamp(midiValue, version == AKAOSNES_V3 ? 1 : 0, 127));
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
    depthDb = tremoloDepthDbForAmplitude(v4PhaseHighByteAmplitude(rate, depth) / 4.0);
  }
  else {
    depthDb = tremoloDepthDb(version, rate, depth);
  }

  return midiValueForDepthRange(depthDb, maxTremoloDepthDb(version));
}

uint8_t rateMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency) {
  return midiValueForHertzInRange(lfoRateHz(version, rate, depth, timer0Frequency),
                                  minLfoRateHz(version),
                                  maxLfoRateHz(version));
}

uint8_t delayMidiValue(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency) {
  // The configured vibrato delay source drives LFO delay within the version's exported seconds range.
  return midiValueForSecondsInRange(delaySeconds(version, delay, tempo, timer0Frequency),
                                    0.0,
                                    maxDelaySeconds(version));
}

uint32_t v1VibratoRampTicks(uint8_t rate, uint8_t tempo) {
  const uint8_t counter = v1RateCounter(rate);
  if (counter == 0) {
    return 0;
  }

  // FF4 derives the fade length from the vibrato rate counter in driver frames,
  // then the sequencer tempo determines how many music ticks those frames span.
  const double rampFrames = 14.0 * (counter + 1);
  return driverFramesToTicks(rampFrames, tempo);
}

uint32_t v3LfoRampTicks(uint8_t rate, uint8_t tempo) {
  // V3's delayed note-start ramp reaches full depth after six LFO update
  // intervals; export that as one smooth SF2/DLS depth fade.
  const double rampFrames = 6.0 * effectiveRateFrames(AKAOSNES_V3, rate, 0);
  return driverFramesToTicks(rampFrames, tempo);
}

uint32_t v4VibratoRampTicks(uint8_t rate, uint8_t tempo) {
  // V4 starts delayed vibrato at quarter slope and reaches full depth after
  // seven rate intervals. Approximate that slope ramp as one smooth depth fade.
  const double rampFrames = 7.0 * effectiveRateFrames(AKAOSNES_V4, rate, 0);
  return driverFramesToTicks(rampFrames, tempo);
}

}  // namespace akao_snes::modulation

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnesV4Lfo.h"

#include <algorithm>
#include <cmath>

namespace vgmtrans::formats::akao_snes {

using namespace core;

namespace {

enum class Engine {
  Rs2,
  PhaseAccumulator,
  LateStepped,
};

[[nodiscard]] constexpr Engine engineFor(AkaoSnesMinorVersion version) {
  // RS2 and the late drivers hold a signed offset until their counter expires.
  // The intervening drivers continuously advance a phase accumulator.
  if (version == AKAOSNES_V4_RS2) {
    return Engine::Rs2;
  }
  if (version == AKAOSNES_V4_RS3 || version == AKAOSNES_V4_GH || version == AKAOSNES_V4_BSGAME) {
    return Engine::LateStepped;
  }
  return Engine::PhaseAccumulator;
}

[[nodiscard]] constexpr u16 halfCycleFrames(Engine engine, u8 rate) {
  return engine == Engine::Rs2 ? static_cast<u16>(rate) + 1 : (rate == 0 ? 256 : rate);
}

[[nodiscard]] double highByteAmplitude(Engine engine, u8 rate, u8 depth) {
  if (depth == 0) {
    return 0.0;
  }
  if (engine != Engine::PhaseAccumulator) {
    return ((depth & 0x3f) << 1) + 1;
  }

  // This driver family divides the requested magnitude by the half-cycle
  // length, quantizes the fixed-point increment, and enforces a nonzero step.
  const u16 frames = halfCycleFrames(engine, rate);
  const u16 step = 4 * std::max<u16>(1, static_cast<u16>(64 * ((depth & 0x3f) + 1) / frames));
  return static_cast<double>(step * frames) / 256.0;
}

[[nodiscard]] constexpr LfoPolarity polarity(Engine engine, u8 depth, u8 delay) {
  if (engine == Engine::Rs2) {
    if (depth >= 0xc0) {
      return LfoPolarity::Bipolar;
    }
    return (depth & 0x40) != 0 ? LfoPolarity::Positive : LfoPolarity::Negative;
  }
  if (engine == Engine::LateStepped) {
    if (depth >= 0xc0) {
      return LfoPolarity::Bipolar;
    }
    return depth >= 0x80 ? LfoPolarity::Negative : LfoPolarity::Positive;
  }

  // The phase-accumulator driver reaches both sides only through its delayed
  // widening sequence. Without it, the $c0 mode starts as an upward triangle.
  if (depth >= 0xc0 && delay != 0) {
    return LfoPolarity::Bipolar;
  }
  return depth >= 0x80 && depth < 0xc0 ? LfoPolarity::Negative : LfoPolarity::Positive;
}

[[nodiscard]] constexpr double initialPhase(Engine engine, LfoPolarity polarity, u8 delay) {
  if (engine == Engine::PhaseAccumulator) {
    return polarity == LfoPolarity::Positive ? 0.75 : polarity == LfoPolarity::Negative ? 0.25 : 0.0;
  }
  if (polarity == LfoPolarity::Negative) {
    return 0.5;
  }
  // An immediate late bipolar LFO first holds its negative endpoint. Its
  // delayed widening sequence begins on the positive side.
  return engine == Engine::LateStepped && polarity == LfoPolarity::Bipolar && delay == 0 ? 0.5 : 0.0;
}

[[nodiscard]] ModulationRange pitchRange(LfoPolarity polarity, double positiveRatio, double negativeRatio) {
  const double upward = 12.0 * std::log2(1.0 + positiveRatio);
  const double downward = 12.0 * std::log2(std::max(1.0 / 65536.0, 1.0 - negativeRatio));
  return ModulationRange{
      .minimum = polarity == LfoPolarity::Positive ? 0.0 : downward,
      .maximum = polarity == LfoPolarity::Negative ? 0.0 : upward,
  };
}

}  // namespace

AkaoSnesV4Lfo akaoSnesV4Lfo(AkaoSnesProfile profile, u8 rate, u8 depth, u8 delay) {
  const Engine engine = engineFor(profile.minorVersion);
  const u16 frames = halfCycleFrames(engine, rate);
  const double lfoAmplitude = highByteAmplitude(engine, rate, depth);
  const LfoPolarity lfoPolarity = polarity(engine, depth, delay);
  // The late drivers first scale the note pitch by the music-channel table's
  // $0f entry, then multiply that result by the signed 8-bit LFO value.
  const double pitchScale = engine == Engine::Rs2           ? 1.0 / 128.0
                            : engine == Engine::LateStepped ? 15.0 / 65536.0
                                                            : 15.0 / 32768.0;
  const double positivePitchRatio = lfoAmplitude * pitchScale;
  // The late engine forms negative values with one's complement, so its
  // negative held endpoint is one unit farther from zero.
  const double negativeAmplitude =
      engine == Engine::LateStepped && depth >= 0x80 ? lfoAmplitude + 1.0 : lfoAmplitude;
  const double negativePitchRatio = negativeAmplitude * pitchScale;
  const ModulationRange range = pitchRange(lfoPolarity, positivePitchRatio, negativePitchRatio);

  return AkaoSnesV4Lfo{
      .rateHertz = akaoSnesFrameRateHz(akaoSnesTimer0Frequency(profile.version, profile.minorVersion)) / (2.0 * frames),
      .vibratoDepthSemitones = std::max(std::abs(range.minimum), std::abs(range.maximum)),
      .tremoloDepthLinearGain = std::max(lfoAmplitude, negativeAmplitude) / 128.0,
      .context =
          {
              .shape = LfoShape{
                  .waveform = engine == Engine::PhaseAccumulator ? LfoWaveform::Triangle : LfoWaveform::Square,
              },
              .polarity = lfoPolarity,
              .initialPhaseCycles = initialPhase(engine, lfoPolarity, delay),
              .pitchRangeSemitones = range,
              .steppedDepthAttackSteps = delay == 0 ? 0u : 4u,
              .sampleImmediatelyOnNote = true,
          },
  };
}

}  // namespace vgmtrans::formats::akao_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vgmtrans::core {

namespace {

[[nodiscard]] int nonnegative(int value) {
  return std::max(value, 0);
}

[[nodiscard]] const std::array<unsigned long, 160>& psxRateTable() {
  static const std::array<unsigned long, 160> table = [] {
    std::array<unsigned long, 160> rates{};
    u32 rate = 3;
    u32 step = 1;
    u32 divider = 0;
    for (int i = 32; i < 160; ++i) {
      if (rate < 0x3fffffffu) {
        rate += step;
        ++divider;
        if (divider == 5) {
          divider = 1;
          step *= 2;
        }
      }
      rate = std::min(rate, 0x3fffffffu);
      rates[static_cast<size_t>(i)] = rate;
    }
    return rates;
  }();
  return table;
}

// SF2 and DLS use a constant rate of decibel attenuation, while a linear SPU
// stage changes amplitude at a constant rate. This adjustment keeps the heard
// fade shape closer after conversion.
[[nodiscard]] double linearAmplitudeDecayToDbDecay(double secondsToFullAttenuation) {
  if (secondsToFullAttenuation <= 0.0) {
    return 0.0;
  }

  constexpr double targetDbLeastSquares = 70.0;
  constexpr double targetDbInitialSlope = 140.0;
  constexpr double ln10 = 2.302585092994046;
  constexpr double kneeSeconds = 0.12;
  constexpr double kneePower = 2.0;

  const double shortScale = targetDbInitialSlope / (20.0 / ln10);
  const double longScale = targetDbLeastSquares * ln10 / 45.0;
  const double x = secondsToFullAttenuation / kneeSeconds;
  const double weight = 1.0 / (1.0 + std::pow(x, kneePower));
  return secondsToFullAttenuation * (weight * shortScale + (1.0 - weight) * longScale);
}

[[nodiscard]] u32 envelopeMicros(double seconds) {
  return static_cast<u32>(
      std::clamp(std::llround(seconds * 1'000'000.0), 0ll, static_cast<long long>(std::numeric_limits<u32>::max())));
}

}  // namespace

Envelope psxSpuEnvelope(u16 adsr1, u16 adsr2, PsxSpuGeneration generation) {
  u8 attackMode = (adsr1 & 0x8000) >> 15;
  u8 attackRate = (adsr1 & 0x7f00) >> 8;
  u8 decayRate = (adsr1 & 0x00f0) >> 4;
  const u8 sustainLevel = adsr1 & 0x000f;
  const u8 sustainMode = (adsr2 & 0x8000) >> 15;
  const u8 sustainDirection = (adsr2 & 0x4000) >> 14;
  const u8 sustainRate = (adsr2 >> 6) & 0x7f;
  u8 releaseMode = (adsr2 & 0x0020) >> 5;
  u8 releaseRate = adsr2 & 0x001f;
  const auto& rates = psxRateTable();
  const double sampleRate =
      generation == PsxSpuGeneration::Ps2 ? static_cast<double>(kPs2SpuSampleRate) : kPs1SpuSampleRate;

  double samples = 0.0;
  if ((attackRate ^ 0x7f) < 0x10) {
    attackRate = 0;
  }
  if (attackMode == 0) {
    const u32 rate = rates[nonnegative((attackRate ^ 0x7f) - 0x10) + 32];
    samples = std::ceil(0x7fffffff / static_cast<double>(rate));
  } else {
    u32 rate = rates[nonnegative((attackRate ^ 0x7f) - 0x10) + 32];
    samples = 0x60000000 / rate;
    const u32 remainder = 0x60000000 % rate;
    rate = rates[nonnegative((attackRate ^ 0x7f) - 0x18) + 32];
    samples += std::ceil(std::max(0.0, 0x1fffffff - static_cast<double>(remainder)) / static_cast<double>(rate));
  }
  const double attackSeconds = samples / sampleRate;

  long envelopeLevel = 0x7fffffff;
  bool sustainLevelFound = false;
  u32 realSustainLevel = 0;
  int steps = 0;
  for (; envelopeLevel > 0; ++steps) {
    if (4 * (decayRate ^ 0x1f) < 0x18) {
      decayRate = 0;
    }
    switch ((envelopeLevel >> 28) & 0x7) {
      case 0:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 0) + 32];
        break;
      case 1:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 4) + 32];
        break;
      case 2:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 6) + 32];
        break;
      case 3:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 8) + 32];
        break;
      case 4:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 9) + 32];
        break;
      case 5:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 10) + 32];
        break;
      case 6:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 11) + 32];
        break;
      case 7:
        envelopeLevel -= rates[nonnegative((4 * (decayRate ^ 0x1f)) - 0x18 + 12) + 32];
        break;
      default:
        break;
    }
    if (!sustainLevelFound && ((envelopeLevel >> 27) & 0xf) <= sustainLevel) {
      realSustainLevel = envelopeLevel;
      sustainLevelFound = true;
    }
  }
  double decaySeconds = steps / sampleRate;

  envelopeLevel = 0x7fffffff;
  double sustainSeconds = -1.0;
  if (sustainDirection != 0 && sustainRate != 0x7f) {
    if (sustainMode == 0) {
      const u32 rate = rates[nonnegative((sustainRate ^ 0x7f) - 0x0f) + 32];
      samples = std::ceil(0x7fffffff / static_cast<double>(rate));
    } else {
      steps = 0;
      while (envelopeLevel > 0) {
        long envelopeLevelDiff = 0;
        long envelopeLevelTarget = 0;
        switch ((envelopeLevel >> 28) & 0x7) {
          case 0:
            envelopeLevelTarget = 0x00000000;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 0) + 32];
            break;
          case 1:
            envelopeLevelTarget = 0x0fffffff;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 4) + 32];
            break;
          case 2:
            envelopeLevelTarget = 0x1fffffff;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 6) + 32];
            break;
          case 3:
            envelopeLevelTarget = 0x2fffffff;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 8) + 32];
            break;
          case 4:
            envelopeLevelTarget = 0x3fffffff;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 9) + 32];
            break;
          case 5:
            envelopeLevelTarget = 0x4fffffff;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 10) + 32];
            break;
          case 6:
            envelopeLevelTarget = 0x5fffffff;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 11) + 32];
            break;
          case 7:
            envelopeLevelTarget = 0x6fffffff;
            envelopeLevelDiff = rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + 12) + 32];
            break;
          default:
            break;
        }
        const long stepCount = (envelopeLevel - envelopeLevelTarget + (envelopeLevelDiff - 1)) / envelopeLevelDiff;
        envelopeLevel -= envelopeLevelDiff * stepCount;
        steps += static_cast<int>(stepCount);
      }
      samples = steps;
    }
    sustainSeconds = linearAmplitudeDecayToDbDecay(samples / sampleRate);
  }

  if (sustainLevel == 0) {
    realSustainLevel = 0x07ffffff;
  }
  double sustainAmplitude = realSustainLevel / static_cast<double>(0x7fffffff);
  if ((decaySeconds < 2.0 || (decayRate >= 0x0e && sustainLevel >= 0x0c)) && sustainRate < 0x7e &&
      sustainDirection == 1) {
    sustainAmplitude = 0.0;
    decaySeconds = sustainSeconds;
  }

  envelopeLevel = 0x7fffffff;
  if (releaseMode == 0) {
    const u32 rate = rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x0c) + 32];
    samples = rate != 0 ? std::ceil(static_cast<double>(envelopeLevel) / rate) : 0;
  } else {
    if ((releaseRate ^ 0x1f) * 4 < 0x18) {
      releaseRate = 0;
    }
    steps = 0;
    for (; envelopeLevel > 0; ++steps) {
      switch ((envelopeLevel >> 28) & 0x7) {
        case 0:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 0) + 32];
          break;
        case 1:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 4) + 32];
          break;
        case 2:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 6) + 32];
          break;
        case 3:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 8) + 32];
          break;
        case 4:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 9) + 32];
          break;
        case 5:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 10) + 32];
          break;
        case 6:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 11) + 32];
          break;
        case 7:
          envelopeLevel -= rates[nonnegative((4 * (releaseRate ^ 0x1f)) - 0x18 + 12) + 32];
          break;
        default:
          break;
      }
    }
    samples = steps;
  }
  const double releaseSeconds = linearAmplitudeDecayToDbDecay(samples / sampleRate);

  return Envelope{
      .attack = envelopeMicros(attackSeconds),
      .decay = decaySeconds < 0.0 ? kEnvelopeInfinite : envelopeMicros(decaySeconds),
      .sustain = static_cast<u32>(std::round(sustainAmplitude * 1000.0)),
      .release = envelopeMicros(releaseSeconds),
      .attackSeconds = attackSeconds,
      .decaySeconds = decaySeconds,
      .releaseSeconds = releaseSeconds,
      .sustainAmplitude = sustainAmplitude,
  };
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/PsxSpu.h"

#include "value/synth/SynthMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace vgmtrans::core {

namespace {

// Exponential stages select a rate adjustment from the current amplitude band.
constexpr std::array<int, 8> kExponentialRateOffsets{0, 4, 6, 8, 9, 10, 11, 12};

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

}  // namespace

Envelope psxSpuEnvelope(u16 adsr1, u16 adsr2, PsxSpuGeneration generation) {
  u8 attackMode = (adsr1 & 0x8000) >> 15;
  u8 attackRate = (adsr1 & 0x7f00) >> 8;
  const u8 decayRate = (adsr1 & 0x00f0) >> 4;
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
    const int rateOffset = kExponentialRateOffsets[(envelopeLevel >> 28) & 0x7];
    envelopeLevel -= rates[nonnegative(4 * (decayRate ^ 0x1f) - 0x18 + rateOffset) + 32];
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
        const long band = (envelopeLevel >> 28) & 0x7;
        const long envelopeLevelTarget = band == 0 ? 0 : (band << 28) - 1;
        const long envelopeLevelDiff =
            rates[nonnegative((sustainRate ^ 0x7f) - 0x1b + kExponentialRateOffsets[band]) + 32];
        const long stepCount = (envelopeLevel - envelopeLevelTarget + (envelopeLevelDiff - 1)) / envelopeLevelDiff;
        envelopeLevel -= envelopeLevelDiff * stepCount;
        steps += static_cast<int>(stepCount);
      }
      samples = steps;
    }
    sustainSeconds = linearAmplitudeFadeToDbEnvelopeSeconds(samples / sampleRate);
  }

  if (sustainLevel == 0) {
    realSustainLevel = 0x07ffffff;
  }
  const double sustainAmplitude = realSustainLevel / static_cast<double>(0x7fffffff);
  const bool hasSecondDecay = sustainDirection != 0;

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
      const int rateOffset = kExponentialRateOffsets[(envelopeLevel >> 28) & 0x7];
      envelopeLevel -= rates[nonnegative(4 * (releaseRate ^ 0x1f) - 0x18 + rateOffset) + 32];
    }
    samples = steps;
  }
  const double releaseSeconds = linearAmplitudeFadeToDbEnvelopeSeconds(samples / sampleRate);

  return Envelope{
      .attackSeconds = attackSeconds,
      .decaySeconds = decaySeconds < 0.0 ? std::numeric_limits<double>::infinity() : decaySeconds,
      .secondDecaySeconds =
          hasSecondDecay ? std::optional{sustainRate == 0x7f ? std::numeric_limits<double>::infinity() : sustainSeconds}
                         : std::nullopt,
      .releaseSeconds = releaseSeconds,
      .sustainAmplitude = sustainAmplitude,
  };
}

}  // namespace vgmtrans::core

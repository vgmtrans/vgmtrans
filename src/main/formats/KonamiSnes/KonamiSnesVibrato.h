/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include "KonamiSnesDefinitions.h"
#include "Modulation.h"

namespace konami_snes::vibrato {

// V1/V2 advance the phase directly once per music tick. Rates above 0x80 wrap around to the same
// speed in the opposite phase direction, and 0x80 never moves the phase at all.
inline constexpr uint8_t legacyRateStep(uint8_t rate) {
  return (rate == 0 || rate == 0x80)
      ? 0
      : static_cast<uint8_t>((rate < 0x80) ? rate : (0x100 - rate));
}

// V3-V6 accumulate the raw rate byte and only advance the 64-step phase on overflow, so each rate
// bucket maps to a fixed phase step.
inline constexpr uint8_t lateEraRateStep(uint8_t rate) {
  return (rate == 0xff) ? 16 : (rate >= 0x80) ? 8 : (rate >= 0x40) ? 4 : (rate >= 0x20) ? 2 : 1;
}

inline constexpr bool isActive(KonamiSnesVersion version, uint8_t rate, uint8_t depth) {
  return depth != 0 && (usesLegacyVibrato(version) ? (legacyRateStep(rate) != 0) : (rate != 0));
}

// Converts a raw vibrato depth byte into the full exported pitch range for the instrument.
inline double maxDepthCents(KonamiSnesVersion version, uint8_t depth) {
  if (usesLegacyVibrato(version)) {
    return (depth < 0x80) ? (depth * (100.0 / 32.0)) : (depth * (100.0 / 8.0));
  }

  return (depth < 0x80) ? (depth * (100.0 / 128.0)) : ((depth - 126.0) * 50.0);
}

// Legacy depth mode is chosen from the target E4 depth byte. Later drivers switch depth curves
// once the live 8.8 fade depth crosses 0x80.
inline double currentDepthCents(KonamiSnesVersion version, uint8_t targetDepth, uint16_t currentDepth) {
  if (usesLegacyVibrato(version)) {
    return (targetDepth < 0x80)
        ? (currentDepth * (100.0 / (32.0 * 256.0)))
        : (currentDepth * (100.0 / (8.0 * 256.0)));
  }

  return (currentDepth < 0x8000)
      ? (currentDepth * (100.0 / (128.0 * 256.0)))
      : ((currentDepth - (126.0 * 256.0)) * (50.0 / 256.0));
}

inline constexpr uint16_t defaultMaxRateFactor(KonamiSnesVersion version) {
  return usesLegacyVibrato(version)
      ? static_cast<uint16_t>(kDefaultLegacyVibratoMaxRateStep * 0xff)
      : static_cast<uint16_t>(0xff * lateEraRateStep(0xff));
}

// The tracked first-pass max only needs a minimum sensible range, not the full default export
// range.
inline constexpr uint16_t minMaxRateFactor(KonamiSnesVersion version) {
  return usesLegacyVibrato(version)
      ? static_cast<uint16_t>(kMinVibratoMaxRateStep * 0xff)
      : kMinVibratoMaxRateStep;
}

// Both driver families export as "base Hz times a factor", but later vibrato runs on a 64-step
// phase with overflow timing instead of a direct 256-step phase increment.
inline double baseHz(KonamiSnesVersion version) {
  return usesLegacyVibrato(version) ? (kTimerHz / 65536.0) : (kTimerHz / 16384.0);
}

inline uint16_t rateFactor(KonamiSnesVersion version, uint8_t rate, uint8_t tempo) {
  if (usesLegacyVibrato(version)) {
    const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
    return static_cast<uint16_t>(legacyRateStep(rate) * safeTempo);
  }

  return (rate == 0) ? 0 : static_cast<uint16_t>(rate * lateEraRateStep(rate));
}

// Legacy delay is measured in music ticks, so it shrinks in real time as tempo rises. Later delay
// is already measured against the fixed 250 Hz update rate.
inline double delaySeconds(KonamiSnesVersion version, uint8_t delay, uint8_t tempo) {
  if (usesLegacyVibrato(version)) {
    const uint8_t safeTempo = (tempo == 0) ? 1 : tempo;
    return ((256.0 / kTimerHz) * (delay + 1.0)) / safeTempo;
  }

  return (delay + 1.0) / kTimerHz;
}

inline double minDelaySeconds(KonamiSnesVersion version) {
  return usesLegacyVibrato(version) ? delaySeconds(version, 0, 0xff) : delaySeconds(version, 0, 0);
}

inline double maxDelaySeconds(KonamiSnesVersion version) {
  return usesLegacyVibrato(version) ? delaySeconds(version, 0xff, 1) : delaySeconds(version, 0xc7, 0);
}

// V3-V6 overload E4 arg1: 00-C7 is delay, C8-FF is an inline fade length with zero delay.
inline constexpr uint8_t delayFromArg1(KonamiSnesVersion version, uint8_t arg1) {
  return (!usesLegacyVibrato(version) && arg1 >= kLateEraVibratoFadeThreshold) ? 0 : arg1;
}

inline constexpr uint8_t inlineFadeLength(KonamiSnesVersion version, uint8_t arg1) {
  return (!usesLegacyVibrato(version) && arg1 >= kLateEraVibratoFadeThreshold)
      ? static_cast<uint8_t>(arg1 - (kLateEraVibratoFadeThreshold - 1))
      : 0;
}

inline StandardVibratoProfile exportProfile(
    KonamiSnesVersion version,
    uint8_t maxDepth = kDefaultVibratoMaxDepth,
    uint16_t maxRateFactor = 0) {
  const uint8_t clampedMaxDepth = std::max(maxDepth, kMinVibratoMaxDepth);
  const uint16_t effectiveMaxRateFactor = (maxRateFactor != 0)
      ? maxRateFactor
      : defaultMaxRateFactor(version);
  const uint16_t clampedMaxRateFactor =
      std::max(effectiveMaxRateFactor, minMaxRateFactor(version));
  const double minHertz = baseHz(version);

  return {
      maxDepthCents(version, clampedMaxDepth),
      minHertz,
      minHertz * clampedMaxRateFactor,
      DelayRange {
          minDelaySeconds(version),
          maxDelaySeconds(version),
      },
  };
}

}  // namespace konami_snes::vibrato

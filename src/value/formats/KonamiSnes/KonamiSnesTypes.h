/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"

#include <algorithm>
#include <optional>

namespace vgmtrans::formats::konami_snes {

enum KonamiSnesVersion : u8 {
  KONAMISNES_NONE = 0,
  KONAMISNES_V1,
  KONAMISNES_V2,
  KONAMISNES_V3,
  KONAMISNES_V4,
  KONAMISNES_V5,
  KONAMISNES_V6,
};

inline constexpr u32 kKonamiSnesAramSize = 0x10000;
inline constexpr u32 kKonamiSnesMaxTracks = 8;
inline constexpr u16 kKonamiSnesPpqn = 48;
inline constexpr u16 kKonamiSnesDefaultPitchBendRangeCents = 200;
inline constexpr u8 kKonamiSnesDefaultTempo = 0xff;
inline constexpr double kKonamiSnesTimerHz = 250.0;
inline constexpr u8 kDefaultVibratoMaxDepth = 0xff;
inline constexpr u8 kDefaultLegacyVibratoMaxRateStep = 0x7f;
inline constexpr u8 kMinVibratoMaxDepth = 0x10;
inline constexpr u8 kMinVibratoMaxRateStep = 0x09;
inline constexpr u8 kLateEraVibratoFadeThreshold = 0xc8;

[[nodiscard]] constexpr bool usesLegacyInstrumentLayout(KonamiSnesVersion version) {
  return version >= KONAMISNES_V1 && version <= KONAMISNES_V3;
}

[[nodiscard]] constexpr u32 instrumentHeaderSize(KonamiSnesVersion version) {
  return usesLegacyInstrumentLayout(version) ? 8 : 7;
}

[[nodiscard]] constexpr u8 noteDurationRateMax(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 ? 100 : 127;
}

[[nodiscard]] constexpr u8 timerFrequency(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 ? 0x20 : 0x40;
}

struct KonamiVibratoSpec {
  double maxDepthCents = 0.0;
  double minHertz = 0.0;
  double maxHertz = 0.0;
};

namespace vibrato {

[[nodiscard]] constexpr bool usesLegacy(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 || version == KONAMISNES_V2;
}

[[nodiscard]] constexpr u8 legacyRateStep(u8 rate) {
  return (rate == 0 || rate == 0x80) ? 0 : static_cast<u8>((rate < 0x80) ? rate : (0x100 - rate));
}

[[nodiscard]] constexpr u8 lateEraRateStep(u8 rate) {
  return (rate == 0xff) ? 16 : (rate >= 0x80) ? 8 : (rate >= 0x40) ? 4 : (rate >= 0x20) ? 2 : 1;
}

[[nodiscard]] constexpr bool isActive(KonamiSnesVersion version, u8 rate, u8 depth) {
  return depth != 0 && (usesLegacy(version) ? (legacyRateStep(rate) != 0) : (rate != 0));
}

[[nodiscard]] inline double maxDepthCents(KonamiSnesVersion version, u8 depth) {
  if (usesLegacy(version)) {
    return (depth < 0x80) ? (depth * (100.0 / 32.0)) : (depth * (100.0 / 8.0));
  }
  return (depth < 0x80) ? (depth * (100.0 / 128.0)) : ((depth - 126.0) * 50.0);
}

[[nodiscard]] inline double currentDepthCents(KonamiSnesVersion version, u8 targetDepth, u16 currentDepth) {
  if (usesLegacy(version)) {
    return (targetDepth < 0x80) ? (currentDepth * (100.0 / (32.0 * 256.0)))
                                : (currentDepth * (100.0 / (8.0 * 256.0)));
  }
  return (currentDepth < 0x8000) ? (currentDepth * (100.0 / (128.0 * 256.0)))
                                 : ((currentDepth - (126.0 * 256.0)) * (50.0 / 256.0));
}

[[nodiscard]] constexpr u16 defaultMaxRateFactor(KonamiSnesVersion version) {
  return usesLegacy(version) ? static_cast<u16>(kDefaultLegacyVibratoMaxRateStep * 0xff)
                             : static_cast<u16>(0xff * lateEraRateStep(0xff));
}

[[nodiscard]] constexpr u16 minMaxRateFactor(KonamiSnesVersion version) {
  return usesLegacy(version) ? static_cast<u16>(kMinVibratoMaxRateStep * 0xff) : kMinVibratoMaxRateStep;
}

[[nodiscard]] inline double baseHz(KonamiSnesVersion version) {
  return usesLegacy(version) ? (kKonamiSnesTimerHz / 65536.0) : (kKonamiSnesTimerHz / 16384.0);
}

[[nodiscard]] inline u16 rateFactor(KonamiSnesVersion version, u8 rate, u8 tempo) {
  if (usesLegacy(version)) {
    const u8 safeTempo = (tempo == 0) ? 1 : tempo;
    return static_cast<u16>(legacyRateStep(rate) * safeTempo);
  }
  return (rate == 0) ? 0 : static_cast<u16>(rate * lateEraRateStep(rate));
}

[[nodiscard]] constexpr u8 delayFromArg1(KonamiSnesVersion version, u8 arg1) {
  return (!usesLegacy(version) && arg1 >= kLateEraVibratoFadeThreshold) ? 0 : arg1;
}

[[nodiscard]] constexpr u8 inlineFadeLength(KonamiSnesVersion version, u8 arg1) {
  return (!usesLegacy(version) && arg1 >= kLateEraVibratoFadeThreshold)
             ? static_cast<u8>(arg1 - (kLateEraVibratoFadeThreshold - 1))
             : 0;
}

[[nodiscard]] inline KonamiVibratoSpec modulationSpec(KonamiSnesVersion version,
                                                      u8 maxDepth = kDefaultVibratoMaxDepth,
                                                      u16 maxRateFactor = 0) {
  const u8 clampedMaxDepth = std::max(maxDepth, kMinVibratoMaxDepth);
  const u16 effectiveMaxRateFactor = maxRateFactor != 0 ? maxRateFactor : defaultMaxRateFactor(version);
  const u16 clampedMaxRateFactor = std::max(effectiveMaxRateFactor, minMaxRateFactor(version));
  const double minHertz = baseHz(version);
  return KonamiVibratoSpec{
      .maxDepthCents = maxDepthCents(version, clampedMaxDepth),
      .minHertz = minHertz,
      .maxHertz = minHertz * clampedMaxRateFactor,
  };
}

}  // namespace vibrato

}  // namespace vgmtrans::formats::konami_snes

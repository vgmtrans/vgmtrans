/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"

#include <cstdint>
#include "KonamiSnesFormat.h"

namespace konami_snes {

inline constexpr double kTimerHz = 250.0;
inline constexpr u8 kDefaultVibratoMaxDepth = 0xff;
inline constexpr u8 kDefaultLegacyVibratoMaxRateStep = 0x7f;
inline constexpr u8 kMinVibratoMaxDepth = 0x10;
inline constexpr u8 kMinVibratoMaxRateStep = 0x09;
inline constexpr u8 kLateEraVibratoFadeThreshold = 0xc8;
inline constexpr u8 kVibratoDelayController = 93;

// V1 and V2 use the older Contra-style vibrato. V3-V6 use the later Goemon-style logic.
inline constexpr bool usesLegacyVibrato(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 || version == KONAMISNES_V2;
}

}  // namespace konami_snes

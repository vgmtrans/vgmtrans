/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

namespace konami_snes {

inline constexpr double kTimerHz = 250.0;

// The driver advances vibrato phase by the raw rate byte once per music tick, and music ticks
// themselves are scaled by the current tempo byte. Splitting the factors this way lets the
// SoundFont modulators track rate and tempo separately without collapsing all tempos into one
// coarse controller range.
inline constexpr double kVibratoBaseHz = kTimerHz / 65536.0;
inline constexpr double kVibratoMaxRateFactor = 128.0;
inline constexpr double kVibratoMaxTempoFactor = 255.0;
inline constexpr double kVibratoMaxDepthCents = 255.0 * 12.5;

// Konami delays vibrato until the first eligible sustain update after note-on, so even delay 0
// maps more closely to a one-tick wait than to SF2's instantaneous sentinel.
inline constexpr double kVibratoDelayBaseSeconds = 256.0 / kTimerHz;
inline constexpr double kVibratoMaxDelayCountFactor = 256.0;

inline constexpr uint8_t kVibratoTempoController = 91;
inline constexpr uint8_t kVibratoDelayController = 93;

}  // namespace konami_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

namespace konami_snes {

inline constexpr double kTimerHz = 250.0;
inline constexpr uint8_t kDefaultVibratoMaxDepth = 0xff;
inline constexpr uint8_t kDefaultVibratoMaxRateStep = 0x7f;
// We define minimum ranges for vibrato depth and rate, in case the sequence uses a very small range
inline constexpr uint8_t kMinVibratoMaxDepth = 0x10;
// At the default tempo FF, effective step 09 is the first Konami rate that reaches about 8.176 Hz.
inline constexpr uint8_t kMinVibratoMaxRateStep = 0x09;

inline constexpr uint8_t effectiveVibratoRateStep(uint8_t rate) {
  return (rate == 0 || rate == 0x80)
      ? 0
      : static_cast<uint8_t>((rate < 0x80) ? rate : (0x100 - rate));
}

inline constexpr double vibratoDepthCents(uint8_t depth) {
  return (depth < 0x80) ? (depth * (100.0 / 32.0)) : (depth * (100.0 / 8.0));
}

inline constexpr uint16_t kDefaultVibratoMaxRateFactor =
    static_cast<uint16_t>(kDefaultVibratoMaxRateStep * 0xff);
inline constexpr uint16_t kMinVibratoMaxRateFactor =
    static_cast<uint16_t>(kMinVibratoMaxRateStep * 0xff);

// The driver advances vibrato phase by the raw rate byte once per music tick, and music ticks
// themselves are scaled by the current tempo byte. We fold both factors together on the sequence
// side and emit the final effective frequency directly.
inline constexpr double kVibratoBaseHz = kTimerHz / 65536.0;
inline constexpr double kVibratoMaxTempoFactor = 255.0;

// The driver delays vibrato until the first eligible sustain update after note-on, so even delay 0
// maps more closely to a one-tick wait than to SF2's instantaneous sentinel. We fold the inverse
// tempo factor into the emitted delay value, so the helper's delay range spans the final absolute
// delay in seconds.
inline constexpr double kVibratoDelayBaseSeconds = 256.0 / kTimerHz;
inline constexpr double kVibratoMaxDelayCountFactor = 256.0;
inline constexpr double kVibratoMinDelaySeconds = kVibratoDelayBaseSeconds / kVibratoMaxTempoFactor;
inline constexpr double kVibratoMaxDelaySeconds = kVibratoDelayBaseSeconds * kVibratoMaxDelayCountFactor;

inline constexpr uint8_t kVibratoDelayController = 93;

}  // namespace konami_snes

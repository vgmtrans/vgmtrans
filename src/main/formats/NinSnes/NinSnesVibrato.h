/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"

#include <cstdint>
#include "Modulation.h"

namespace nin_snes::vibrato {

constexpr double kTimerHz = 500.0;
constexpr u8 kDefaultTempo = 0x20;
constexpr u8 kDelayController = 93;
constexpr u8 kMinVibratoMaxDepth = 0x80;
constexpr u8 kMinVibratoMaxRate = 0x20;

// Depth 00-F0 uses the regular 1/256-semitone path. F1-FF switches to the large-depth mode that
// reuses the low nibble as a 1-15 semitone multiplier.
inline constexpr double depthCents(u8 depth) {
  const double centsPerPitchUnit = 100.0 / 256.0;
  if (depth <= 0xf0) {
    return ((0xffu * depth) >> 8) * centsPerPitchUnit;
  }
  return (0xffu * (depth & 0x0fu)) * centsPerPitchUnit;
}

inline constexpr double rateHz(u8 rate, double tempo) {
  const double safeTempo = (tempo > 0.0) ? tempo : 1.0;
  return (kTimerHz * safeTempo * rate) / 65536.0;
}

inline constexpr double delaySeconds(u8 delay, double tempo) {
  const double safeTempo = (tempo > 0.0) ? tempo : 1.0;
  return (256.0 * delay) / (kTimerHz * safeTempo);
}

inline constexpr double kMinRateHz = rateHz(1, 1);
inline constexpr double kDefaultMaxRateHz = rateHz(0xff, 0xff);
inline constexpr double kDefaultMaxDepthCents = depthCents(0xff);
// The tracked sequence max only needs a minimum sensible range, not the full format maximum.
inline constexpr double kMinMaxDepthCents = depthCents(kMinVibratoMaxDepth);
inline constexpr double kMinMaxRateHz = rateHz(kMinVibratoMaxRate, kDefaultTempo);
// Delay 00 is "start immediately". The SF2 export helper clamps that to the smallest normal
// positive delay it can represent.
inline constexpr double kMinDelaySeconds = 0.0;
inline constexpr double kMaxDelaySeconds = delaySeconds(0xff, 1);

inline VibratoModulationSpec modulationSpec(double maxDepthCents = kDefaultMaxDepthCents,
                                            double maxRateHz = kDefaultMaxRateHz) {
  return {
      (maxDepthCents > 0.0) ? maxDepthCents : kDefaultMaxDepthCents,
      kMinRateHz,
      (maxRateHz > 0.0) ? maxRateHz : kDefaultMaxRateHz,
      DelayRange {
          kMinDelaySeconds,
          kMaxDelaySeconds,
      },
  };
}

}  // namespace nin_snes::vibrato

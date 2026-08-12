/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <cmath>
#include <limits>

namespace vgmtrans::formats::mp2k {

inline constexpr double kGbaMixerFrameRate = 16777216.0 / 280896.0;

[[nodiscard]] inline double directAttackSeconds(u8 rate) {
  if (rate == 0) {
    return std::numeric_limits<double>::infinity();
  }
  // SoundMainRAM applies the first attack step before the new channel's first
  // audible frame. Give the linear target envelope the exact same area as the
  // stepped 8-bit ramp, including its final clamped step.
  u32 lostLevel = 0;
  for (u32 level = rate; level < 255; level += rate) {
    lostLevel += 255 - level;
  }
  return 2.0 * lostLevel / (255.0 * kGbaMixerFrameRate);
}

[[nodiscard]] inline double directDecaySeconds(u8 rate) {
  if (rate == 0) {
    return 0.0;
  }
  // MP2k's multiplication is a constant dB-per-frame slope. Envelope decay
  // times describe a full 100 dB change, independent of the sustain endpoint.
  constexpr double envelopeRangeDb = 100.0;
  return (envelopeRangeDb / 20.0) * std::log(10.0) / (kGbaMixerFrameRate * std::log(256.0 / rate));
}

[[nodiscard]] inline double directReleaseSeconds(u8 rate) {
  // Preserve the same dB slope across SoundFont's 100 dB release range rather
  // than stretching the 8-bit integer's much earlier underflow to -100 dB.
  // A zero multiplier silences the first release mixer pass.
  return rate == 0 ? 0.0 : directDecaySeconds(rate);
}

[[nodiscard]] inline double cgbEnvelopeSeconds(u8 rate) {
  const u8 period = rate & 7;
  return period == 0 ? 0.0 : 15.0 * period / 64.0;
}

}  // namespace vgmtrans::formats::mp2k

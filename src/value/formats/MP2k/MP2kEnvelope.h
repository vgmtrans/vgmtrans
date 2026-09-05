/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"
#include "value/synth/SynthMath.h"

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
  const u32 steps = 254 / rate;
  const u32 lostLevel = 255 * steps - rate * steps * (steps + 1) / 2;
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

[[nodiscard]] inline double cgbEnvelopeSeconds(u8 counter, u8 levels = 15) {
  return static_cast<double>(levels) * counter / 64.0;
}

[[nodiscard]] inline double cgbDecaySeconds(u8 counter, u8 levels = 15) {
  // CGB envelopes step linearly in amplitude; SF2 and DLS decay linearly in dB.
  return core::linearAmplitudeFadeToDbEnvelopeSeconds(cgbEnvelopeSeconds(counter, levels));
}

}  // namespace vgmtrans::formats::mp2k

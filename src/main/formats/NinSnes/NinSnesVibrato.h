/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

namespace nin_snes::vibrato {

constexpr double kTimerHz = 500.0;
constexpr uint8_t kDefaultTempo = 0x20;
constexpr uint8_t kDelayController = 93;
constexpr uint8_t kMinVibratoMaxDepth = 0x80;
constexpr uint8_t kMinVibratoMaxRate = 0x20;

inline constexpr bool isActive(uint8_t rate, uint8_t depth) {
  return rate != 0 && depth != 0;
}

inline constexpr double sanitizedTempo(double tempo) {
  return (tempo > 0.0) ? tempo : 1.0;
}

// Depth 00-F0 uses the regular 1/256-semitone path. F1-FF switches to the large-depth mode that
// reuses the low nibble as a 1-15 semitone multiplier.
inline constexpr double depthCents(uint8_t depth) {
  const double centsPerPitchUnit = 100.0 / 256.0;
  if (depth <= 0xf0) {
    return ((0xffu * depth) >> 8) * centsPerPitchUnit;
  }
  return (0xffu * (depth & 0x0fu)) * centsPerPitchUnit;
}

inline constexpr double rateHz(uint8_t rate, double tempo) {
  return (kTimerHz * sanitizedTempo(tempo) * rate) / 65536.0;
}

inline constexpr double delaySeconds(uint8_t delay, double tempo) {
  return (256.0 * delay) / (kTimerHz * sanitizedTempo(tempo));
}

inline constexpr double minRateHz() {
  return rateHz(1, 1);
}

inline constexpr double defaultMaxRateHz() {
  return rateHz(0xff, 0xff);
}

inline constexpr double defaultMaxDepthCents() {
  return depthCents(0xff);
}

// The tracked sequence max only needs a minimum sensible range, not the full format maximum.
inline constexpr double minMaxDepthCents() {
  return depthCents(kMinVibratoMaxDepth);
}

inline constexpr double minMaxRateHz() {
  return rateHz(kMinVibratoMaxRate, kDefaultTempo);
}

// Delay 00 is "start immediately". The SF2 export helper clamps that to the smallest normal
// positive delay it can represent.
inline constexpr double minDelaySeconds() {
  return 0.0;
}

inline constexpr double maxDelaySeconds() {
  return delaySeconds(0xff, 1);
}

}  // namespace nin_snes::vibrato

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>
#include "KonamiSnesFormat.h"

namespace konami_snes {

inline constexpr double kTimerHz = 250.0;
inline constexpr uint8_t kDefaultVibratoMaxDepth = 0xff;
inline constexpr uint8_t kDefaultV1VibratoMaxRateStep = 0x7f;
// We define minimum ranges for vibrato depth and rate, in case the sequence uses a very small range
inline constexpr uint8_t kMinVibratoMaxDepth = 0x10;
// At the default tempo FF, effective step 09 is the first Konami rate that reaches about 8.176 Hz.
inline constexpr uint8_t kMinVibratoMaxRateStep = 0x09;

inline constexpr bool usesV1Vibrato(KonamiSnesVersion version) {
  return version == KONAMISNES_V1;
}

inline constexpr uint8_t v1VibratoRateStep(uint8_t rate) {
  return (rate == 0 || rate == 0x80)
      ? 0
      : static_cast<uint8_t>((rate < 0x80) ? rate : (0x100 - rate));
}

inline constexpr uint8_t v2VibratoRateStep(uint8_t rate) {
  return (rate == 0xff) ? 16 : (rate >= 0x80) ? 8 : (rate >= 0x40) ? 4 : (rate >= 0x20) ? 2 : 1;
}

inline constexpr bool hasActiveVibrato(KonamiSnesVersion version, uint8_t rate, uint8_t depth) {
  return depth != 0 && (usesV1Vibrato(version) ? (v1VibratoRateStep(rate) != 0) : (rate != 0));
}

inline constexpr double vibratoDepthCents(KonamiSnesVersion version, uint8_t depth) {
  if (usesV1Vibrato(version)) {
    return (depth < 0x80) ? (depth * (100.0 / 32.0)) : (depth * (100.0 / 8.0));
  }

  return (depth < 0x80) ? (depth * (100.0 / 128.0)) : ((depth - 126.0) * 50.0);
}

inline constexpr uint16_t vibratoRateFactor(KonamiSnesVersion version, uint8_t rate, uint8_t tempo) {
  if (usesV1Vibrato(version)) {
    return static_cast<uint16_t>(v1VibratoRateStep(rate) * (tempo == 0 ? 1 : tempo));
  }

  return (rate == 0) ? 0 : static_cast<uint16_t>(rate * v2VibratoRateStep(rate));
}

inline constexpr uint16_t defaultVibratoMaxRateFactor(KonamiSnesVersion version) {
  return usesV1Vibrato(version)
      ? static_cast<uint16_t>(kDefaultV1VibratoMaxRateStep * 0xff)
      : static_cast<uint16_t>(0xff * v2VibratoRateStep(0xff));
}

inline constexpr uint16_t minVibratoMaxRateFactor(KonamiSnesVersion version) {
  return usesV1Vibrato(version)
      ? static_cast<uint16_t>(kMinVibratoMaxRateStep * 0xff)
      : kMinVibratoMaxRateStep;
}

inline constexpr double vibratoBaseHz(KonamiSnesVersion version) {
  return usesV1Vibrato(version) ? (kTimerHz / 65536.0) : (kTimerHz / 16384.0);
}

// The driver delays vibrato until the first eligible sustain update after note-on, so even delay 0
// maps more closely to a one-tick wait than to SF2's instantaneous sentinel.
inline constexpr double kV1VibratoDelayBaseSeconds = 256.0 / kTimerHz;
inline constexpr double vibratoDelaySeconds(KonamiSnesVersion version, uint8_t delay, uint8_t tempo) {
  if (usesV1Vibrato(version)) {
    return (kV1VibratoDelayBaseSeconds * (delay + 1.0)) / (tempo == 0 ? 1 : tempo);
  }

  return (delay + 1.0) / kTimerHz;
}

inline constexpr double minVibratoDelaySeconds(KonamiSnesVersion version) {
  return usesV1Vibrato(version) ? vibratoDelaySeconds(version, 0, 0xff) : vibratoDelaySeconds(version, 0, 0);
}

inline constexpr double maxVibratoDelaySeconds(KonamiSnesVersion version) {
  return usesV1Vibrato(version) ? vibratoDelaySeconds(version, 0xff, 1) : vibratoDelaySeconds(version, 0xc7, 0);
}

inline constexpr uint8_t vibratoDelayFromArg1(KonamiSnesVersion version, uint8_t arg1) {
  return (!usesV1Vibrato(version) && arg1 >= 0xc8) ? 0 : arg1;
}

inline constexpr uint8_t vibratoInlineFadeLength(KonamiSnesVersion version, uint8_t arg1) {
  return (!usesV1Vibrato(version) && arg1 >= 0xc8) ? static_cast<uint8_t>(arg1 - 0xc7) : 0;
}

inline constexpr uint8_t kVibratoDelayController = 93;

}  // namespace konami_snes

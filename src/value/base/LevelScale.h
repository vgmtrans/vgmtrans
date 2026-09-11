/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <algorithm>
#include <cmath>

namespace vgmtrans::core::LevelScale {

[[nodiscard]] inline double clamp01(double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] inline double linearFromLinear(double value) {
  return clamp01(value);
}

[[nodiscard]] inline double linearFromMidi7(u8 value) {
  const double normalized = static_cast<double>(value) / 127.0;
  return normalized * normalized;
}

[[nodiscard]] inline double linearFromMidi14(u16 value) {
  const double normalized = static_cast<double>(std::min<u16>(value, 16383)) / 16383.0;
  return normalized * normalized;
}

[[nodiscard]] inline u8 midi7FromLinear(double linearGain) {
  return static_cast<u8>(
      std::clamp<int>(static_cast<int>(std::lround(std::sqrt(clamp01(linearGain)) * 127.0)), 0, 127));
}

[[nodiscard]] inline u16 midi14FromLinear(double linearGain) {
  return static_cast<u16>(
      std::clamp<int>(static_cast<int>(std::lround(std::sqrt(clamp01(linearGain)) * 16383.0)), 0, 16383));
}

}  // namespace vgmtrans::core::LevelScale

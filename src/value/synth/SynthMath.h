/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"

namespace vgmtrans::core {

[[nodiscard]] s32 synthAmountFromHertz(double hertz);
[[nodiscard]] s32 synthAmountFromHertzRange(double minHertz, double maxHertz);
[[nodiscard]] s32 synthAmountFromSeconds(double seconds);
[[nodiscard]] s32 synthAmountFromSecondsRange(double minSeconds, double maxSeconds);
[[nodiscard]] s32 synthAmountFromCentibels(double centibels);
[[nodiscard]] s32 synthAmountFromDecibels(double decibels);
[[nodiscard]] double synthSecondsRangeMinimum(double seconds);

}  // namespace vgmtrans::core

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
[[nodiscard]] s32 synthAmountFromCentibels(double centibels);
[[nodiscard]] s32 synthAmountFromDecibels(double decibels);

}  // namespace vgmtrans::core

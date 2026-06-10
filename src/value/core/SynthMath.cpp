/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/SynthMath.h"

#include <cmath>

namespace vgmtrans::core {

namespace {

constexpr double kSf2LfoReferenceHz = 8.176;

}  // namespace

s32 synthAmountFromHertz(double hertz) {
  return static_cast<s32>(std::lround(1200.0 * std::log2(hertz / kSf2LfoReferenceHz)));
}

s32 synthAmountFromHertzRange(double minHertz, double maxHertz) {
  const double minCents = static_cast<double>(synthAmountFromHertz(minHertz));
  const double maxCents = static_cast<double>(synthAmountFromHertz(maxHertz));
  return static_cast<s32>(std::lround((maxCents - minCents) * 128.0 / 127.0));
}

s32 synthAmountFromCentibels(double centibels) {
  return static_cast<s32>(std::lround(centibels));
}

s32 synthAmountFromDecibels(double decibels) {
  return static_cast<s32>(std::lround(decibels * 10.0));
}

}  // namespace vgmtrans::core

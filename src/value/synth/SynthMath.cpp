/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SynthMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vgmtrans::core {

namespace {

constexpr double kSf2LfoReferenceHz = 8.176;
constexpr double kSf2MinNormalDelaySeconds = 1.0 / 1024.0;

}  // namespace

s32 synthAmountFromHertz(double hertz) {
  return static_cast<s32>(std::lround(1200.0 * std::log2(hertz / kSf2LfoReferenceHz)));
}

s32 synthAmountFromHertzRange(double minHertz, double maxHertz) {
  const double minCents = static_cast<double>(synthAmountFromHertz(minHertz));
  const double maxCents = static_cast<double>(synthAmountFromHertz(maxHertz));
  return static_cast<s32>(std::lround((maxCents - minCents) * 128.0 / 127.0));
}

s32 synthAmountFromSeconds(double seconds) {
  if (seconds <= 0.0 || !std::isfinite(seconds)) {
    return std::numeric_limits<s16>::min();
  }

  const double timecents = std::round(1200.0 * std::log2(seconds));
  if (timecents > static_cast<double>(std::numeric_limits<s16>::max())) {
    return std::numeric_limits<s16>::max();
  }
  if (timecents < static_cast<double>(std::numeric_limits<s16>::min())) {
    return std::numeric_limits<s16>::min();
  }
  return static_cast<s32>(timecents);
}

s32 synthAmountFromSecondsRange(double minSeconds, double maxSeconds) {
  const s32 minAmount = synthAmountFromSeconds(synthSecondsRangeMinimum(minSeconds));
  const s32 maxAmount = synthAmountFromSeconds(maxSeconds);
  const double fullScaleRange = (maxAmount - minAmount) * 128.0 / 127.0;
  return static_cast<s32>(std::lround(fullScaleRange));
}

s32 synthAmountFromCentibels(double centibels) {
  return static_cast<s32>(std::lround(centibels));
}

s32 synthAmountFromDecibels(double decibels) {
  return static_cast<s32>(std::lround(decibels * 10.0));
}

double synthSecondsRangeMinimum(double seconds) {
  return std::max(seconds, kSf2MinNormalDelaySeconds);
}

double linearAmplitudeToAttenuationDb(double amplitude, double silenceDb) {
  if (amplitude <= 0.0) {
    return silenceDb;
  }
  return -20.0 * std::log10(std::min(1.0, amplitude));
}

double panPositionFrom7Bit(u8 pan) {
  // Preserve exact center. A 128-step scale would otherwise put value
  // 64 slightly to the right when divided by the maximum value of 127.
  return pan == 64 ? 0.5 : static_cast<double>(pan) / 127.0;
}

}  // namespace vgmtrans::core

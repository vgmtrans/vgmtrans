/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/types.h"
#include "Modulation.h"
#include <algorithm>
#include <cmath>
#include "LogManager.h"
#include "scale_conversion.h"

namespace {

constexpr double kSf2LfoReferenceHz = 8.176;
constexpr double kSf2MinNormalDelaySeconds = 1.0 / 1024.0;

s32 hertzToInstrumentCents(double hertz) {
  return static_cast<s32>(std::lround(1200.0 * std::log2(hertz / kSf2LfoReferenceHz)));
}

// Linearly map an absolute amount in destination units onto a 7-bit unipolar
// MIDI controller value for the [minAmount, maxAmount] range, clamped to 0..127.
u8 midiValueForAmountInRange(s32 currentAmount, s32 minAmount, s32 maxAmount) {
  if (minAmount == maxAmount) {
    return 0;
  }

  const int midiValue = static_cast<int>(std::round(
      128.0 * (currentAmount - minAmount) / static_cast<double>(maxAmount - minAmount)));
  return static_cast<u8>(std::clamp(midiValue, 0, 127));
}

}  // namespace

double clampSecondsRangeMinimum(double seconds) {
  return std::max(seconds, kSf2MinNormalDelaySeconds);
}

ModAmount ModAmount::raw(s32 amount) {
  return ModAmount(amount, true);
}

ModAmount ModAmount::fromCents(double cents) {
  if (!std::isfinite(cents)) {
    L_WARN("ModAmount::fromCents received invalid cents value: {}", cents);
    return ModAmount(0, false);
  }

  return ModAmount(static_cast<s32>(std::lround(cents)), true);
}

ModAmount ModAmount::fromHertz(double hertz) {
  if (hertz <= 0.0 || !std::isfinite(hertz)) {
    L_WARN("ModAmount::fromHertz received invalid hertz value: {}", hertz);
    return ModAmount(0, false);
  }

  return ModAmount(hertzToInstrumentCents(hertz), true);
}

ModAmount ModAmount::fromHertzRange(double minHertz, double maxHertz) {
  if (minHertz <= 0.0 || maxHertz <= 0.0 ||
      !std::isfinite(minHertz) || !std::isfinite(maxHertz)) {
    L_WARN("ModAmount::fromHertzRange received invalid hertz range: minHertz={}, maxHertz={}", minHertz, maxHertz);
    return ModAmount(0, false);
  }

  const double minCents = static_cast<double>(hertzToInstrumentCents(minHertz));
  const double maxCents = static_cast<double>(hertzToInstrumentCents(maxHertz));
  const double fullScaleRange = (maxCents - minCents) * 128.0 / 127.0;
  return ModAmount(static_cast<s32>(std::lround(fullScaleRange)), true);
}

u8 midiValueForHertzInRange(double hertz, double minHertz, double maxHertz) {
  const ModAmount minAmount = ModAmount::fromHertz(minHertz);
  const ModAmount rangeAmount = ModAmount::fromHertzRange(minHertz, maxHertz);
  const ModAmount currentAmount = ModAmount::fromHertz(hertz);

  if (!minAmount.valid() || !rangeAmount.valid() || !currentAmount.valid() || rangeAmount.value() == 0) {
    return 0;
  }

  return midiValueForAmountInRange(currentAmount.value(),
                                   minAmount.value(),
                                   minAmount.value() + rangeAmount.value());
}

ModAmount ModAmount::fromSeconds(double seconds) {
  return ModAmount(secondsToSf2Timecents(seconds), true);
}

ModAmount ModAmount::fromSecondsRange(double minSeconds, double maxSeconds) {
  if (maxSeconds <= 0.0 || !std::isfinite(minSeconds) || !std::isfinite(maxSeconds)) {
    L_WARN("ModAmount::fromSecondsRange received invalid seconds range: minSeconds={}, maxSeconds={}",
           minSeconds, maxSeconds);
    return ModAmount(0, false);
  }

  const ModAmount minAmount = fromSeconds(clampSecondsRangeMinimum(minSeconds));
  const ModAmount maxAmount = fromSeconds(maxSeconds);
  if (!minAmount.valid() || !maxAmount.valid()) {
    return ModAmount(0, false);
  }

  const double fullScaleRange = (maxAmount.value() - minAmount.value()) * 128.0 / 127.0;
  return ModAmount(static_cast<s32>(std::lround(fullScaleRange)), true);
}

u8 midiValueForSecondsInRange(double seconds, double minSeconds, double maxSeconds) {
  const ModAmount minAmount = ModAmount::fromSeconds(clampSecondsRangeMinimum(minSeconds));
  const ModAmount rangeAmount = ModAmount::fromSecondsRange(minSeconds, maxSeconds);
  const ModAmount currentAmount = ModAmount::fromSeconds(clampSecondsRangeMinimum(seconds));

  if (!minAmount.valid() || !rangeAmount.valid() || !currentAmount.valid() || rangeAmount.value() == 0) {
    return 0;
  }

  return midiValueForAmountInRange(currentAmount.value(),
                                   minAmount.value(),
                                   minAmount.value() + rangeAmount.value());
}

ModAmount ModAmount::fromCentibels(double centibels) {
  if (!std::isfinite(centibels)) {
    L_WARN("ModAmount::fromCentibels received invalid centibels value: {}", centibels);
    return ModAmount(0, false);
  }

  return ModAmount(static_cast<s32>(std::lround(centibels)), true);
}

ModAmount ModAmount::fromDecibels(double decibels) {
  if (!std::isfinite(decibels)) {
    L_WARN("ModAmount::fromDecibels received invalid decibels value: {}", decibels);
    return ModAmount(0, false);
  }

  return ModAmount(static_cast<s32>(std::lround(decibels * 10.0)), true);
}

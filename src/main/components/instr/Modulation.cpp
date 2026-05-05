/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Modulation.h"
#include <cmath>
#include "ScaleConversion.h"

namespace {

constexpr double kSf2LfoReferenceHz = 8.176;

int32_t hertzToInstrumentCents(double hertz) {
  return static_cast<int32_t>(std::lround(1200.0 * std::log2(hertz / kSf2LfoReferenceHz)));
}

}  // namespace

ModAmount ModAmount::raw(int32_t amount) {
  return ModAmount(amount, true);
}

ModAmount ModAmount::fromCents(double cents) {
  if (!std::isfinite(cents)) {
    return ModAmount(0, false);
  }

  return ModAmount(static_cast<int32_t>(std::lround(cents)), true);
}

ModAmount ModAmount::fromHertz(double hertz) {
  if (hertz <= 0.0 || !std::isfinite(hertz)) {
    return ModAmount(0, false);
  }

  return ModAmount(hertzToInstrumentCents(hertz), true);
}

ModAmount ModAmount::fromHertzRange(double minHertz, double maxHertz) {
  if (minHertz <= 0.0 || maxHertz <= 0.0 ||
      !std::isfinite(minHertz) || !std::isfinite(maxHertz)) {
    return ModAmount(0, false);
  }

  const double minCents = static_cast<double>(hertzToInstrumentCents(minHertz));
  const double maxCents = static_cast<double>(hertzToInstrumentCents(maxHertz));
  const double fullScaleRange = (maxCents - minCents) * 128.0 / 127.0;
  return ModAmount(static_cast<int32_t>(std::lround(fullScaleRange)), true);
}

ModAmount ModAmount::fromSeconds(double seconds) {
  return ModAmount(secondsToSf2Timecents(seconds), true);
}

ModAmount ModAmount::fromCentibels(double centibels) {
  if (!std::isfinite(centibels)) {
    return ModAmount(0, false);
  }

  return ModAmount(static_cast<int32_t>(std::lround(centibels)), true);
}

ModAmount ModAmount::fromDecibels(double decibels) {
  if (!std::isfinite(decibels)) {
    return ModAmount(0, false);
  }

  return ModAmount(static_cast<int32_t>(std::lround(decibels * 10.0)), true);
}

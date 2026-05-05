/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "InstrumentModulation.h"
#include <cmath>
#include "ScaleConversion.h"

namespace {

constexpr double kSf2LfoReferenceHz = 8.176;

int32_t hertzToInstrumentCents(double hertz) {
  return static_cast<int32_t>(std::lround(1200.0 * std::log2(hertz / kSf2LfoReferenceHz)));
}

}  // namespace

ParamAmount ParamAmount::raw(int32_t amount) {
  return ParamAmount(amount, true);
}

ParamAmount ParamAmount::cents(double cents) {
  if (!std::isfinite(cents)) {
    return ParamAmount(0, false);
  }

  return ParamAmount(static_cast<int32_t>(std::lround(cents)), true);
}

ParamAmount ParamAmount::hertz(double hertz) {
  if (hertz <= 0.0 || !std::isfinite(hertz)) {
    return ParamAmount(0, false);
  }

  return ParamAmount(hertzToInstrumentCents(hertz), true);
}

ParamAmount ParamAmount::hertzRange(double minHertz, double maxHertz) {
  if (minHertz <= 0.0 || maxHertz <= 0.0 ||
      !std::isfinite(minHertz) || !std::isfinite(maxHertz)) {
    return ParamAmount(0, false);
  }

  const double minCents = static_cast<double>(hertzToInstrumentCents(minHertz));
  const double maxCents = static_cast<double>(hertzToInstrumentCents(maxHertz));
  const double fullScaleRange = (maxCents - minCents) * 128.0 / 127.0;
  return ParamAmount(static_cast<int32_t>(std::lround(fullScaleRange)), true);
}

ParamAmount ParamAmount::seconds(double seconds) {
  return ParamAmount(secondsToSf2Timecents(seconds), true);
}

ParamAmount ParamAmount::centibels(double centibels) {
  if (!std::isfinite(centibels)) {
    return ParamAmount(0, false);
  }

  return ParamAmount(static_cast<int32_t>(std::lround(centibels)), true);
}

ParamAmount ParamAmount::attenuationDb(double decibels) {
  if (!std::isfinite(decibels)) {
    return ParamAmount(0, false);
  }

  return ParamAmount(static_cast<int32_t>(std::lround(decibels * 10.0)), true);
}

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SynthMath.h"

#include <algorithm>
#include <cmath>

namespace vgmtrans::core {

double linearAmplitudeToAttenuationDb(double amplitude, double silenceDb) {
  if (amplitude <= 0.0) {
    return silenceDb;
  }
  return -20.0 * std::log10(std::min(1.0, amplitude));
}

double linearAmplitudeFadeToDbEnvelopeSeconds(double secondsToSilence) {
  if (secondsToSilence <= 0.0) {
    return 0.0;
  }
  if (!std::isfinite(secondsToSilence)) {
    return secondsToSilence;
  }

  // A dB-linear envelope falls much faster at the start than a
  // linear-amplitude fade of the same duration. Blend an initial-slope match
  // for short fades with a least-squares match for longer ones.
  constexpr double targetDbLeastSquares = 70.0;
  constexpr double targetDbInitialSlope = 140.0;
  constexpr double ln10 = 2.302585092994046;
  constexpr double kneeSeconds = 0.12;
  const double shortScale = targetDbInitialSlope / (20.0 / ln10);
  const double longScale = targetDbLeastSquares * ln10 / 45.0;
  const double x = secondsToSilence / kneeSeconds;
  const double weight = 1.0 / (1.0 + x * x);
  return secondsToSilence * (weight * shortScale + (1.0 - weight) * longScale);
}

double panPositionFrom7Bit(u8 pan) {
  // Preserve exact center. A 128-step scale would otherwise put value
  // 64 slightly to the right when divided by the maximum value of 127.
  return pan == 64 ? 0.5 : static_cast<double>(pan) / 127.0;
}

}  // namespace vgmtrans::core

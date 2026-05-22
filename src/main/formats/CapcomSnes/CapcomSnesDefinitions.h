/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Modulation.h"
namespace capcom_snes {

inline constexpr double kLfoStepHz = 1000.0 / 16384.0;
inline constexpr double kVibratoBaseHz = kLfoStepHz;
inline constexpr double kVibratoMaxHz = 255.0 * kLfoStepHz;
inline constexpr double kTremoloBaseHz = 2.0 * kLfoStepHz;
inline constexpr double kTremoloMaxHz = 510.0 * kLfoStepHz;
inline constexpr int kTremoloHalfDepthCentibels = 484;
inline constexpr double kTremoloHalfDepthDb = kTremoloHalfDepthCentibels / 10.0;

inline VibratoModulationSpec vibratoModulationSpec() {
  return {
      1200,
      kVibratoBaseHz,
      kVibratoMaxHz,
  };
}

inline TremoloModulationSpec tremoloModulationSpec() {
  return {
      kTremoloHalfDepthDb,
      kTremoloBaseHz,
      kTremoloMaxHz,
      TremoloGainMode::NoBoost,
  };
}

}  // namespace capcom_snes

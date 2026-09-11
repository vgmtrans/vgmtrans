/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "CapcomSnesConstants.h"
#include "Modulation.h"

namespace capcom_snes {

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

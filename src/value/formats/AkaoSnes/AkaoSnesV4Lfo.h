/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/AkaoSnes/AkaoSnes.h"
#include "value/sequence/PerformanceModel.h"

namespace vgmtrans::formats::akao_snes {

// Physical oscillator settings produced by a V4 vibrato or tremolo command.
struct AkaoSnesV4Lfo {
  double rateHertz = 0.0;
  double vibratoDepthSemitones = 0.0;
  double tremoloDepthLinearGain = 0.0;
  core::LfoPerformanceContext context;
};

[[nodiscard]] AkaoSnesV4Lfo akaoSnesV4Lfo(AkaoSnesProfile profile, u8 rate, u8 depth, u8 delay);

}  // namespace vgmtrans::formats::akao_snes

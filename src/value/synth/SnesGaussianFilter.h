/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/synth/SynthModel.h"

namespace vgmtrans::core {

// Applies the tonal response of the S-DSP Gaussian interpolator without
// reproducing its phase-dependent resampling artifacts.
void applySnesGaussianResponseFilter(DecodedSample& sample);

}  // namespace vgmtrans::core

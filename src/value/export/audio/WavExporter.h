/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/synth/SynthModel.h"

#include <vector>

namespace vgmtrans::core {

// Writes decoded PCM16 sample data as a minimal RIFF/WAVE file.
[[nodiscard]] std::vector<u8> encodePcm16Wav(const DecodedSample& sample);

}  // namespace vgmtrans::core

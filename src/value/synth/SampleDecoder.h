/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/synth/SynthModel.h"

#include <optional>
#include <span>

namespace vgmtrans::core {

// Decode functions receive the whole source span and use the Sample's
// encodedData range to locate and validate the encoded bytes.
[[nodiscard]] std::optional<DecodedSample> decodeSample(const Sample& sample, std::span<const u8> sourceBytes);

}  // namespace vgmtrans::core

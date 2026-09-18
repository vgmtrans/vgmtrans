/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/scan/SourceExtractor.h"

namespace vgmtrans::formats::cue {

// Mode 1 and Mode 2 tracks become concatenated user payloads, with one derived
// source per track. XA subheaders select 2048-byte Form 1 or 2324-byte Form 2 data.
[[nodiscard]] core::SourceExtractor cueExtractor();

}  // namespace vgmtrans::formats::cue

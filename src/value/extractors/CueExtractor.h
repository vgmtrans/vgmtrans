/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/scan/SourceExtractor.h"

namespace vgmtrans::formats::cue {

// MODE2/2352 and MODE2/2336 tracks become concatenated XA user payloads
// (2048 bytes for Form 1, 2324 for Form 2), with one derived source per track.
[[nodiscard]] core::SourceExtractor cueExtractor();

}  // namespace vgmtrans::formats::cue

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/Source.h"
#include "value/formats/NDS/NdsSequenceProgram.h"

namespace vgmtrans::formats::nds {

[[nodiscard]] NdsSequenceRange ndsSequenceRangeForFatEntry(core::ByteReader reader, u32 offset, u32 size);

}  // namespace vgmtrans::formats::nds

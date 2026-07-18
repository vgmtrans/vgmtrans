/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/formats/NDS/NdsTypes.h"

#include <optional>
#include <vector>

namespace vgmtrans::formats::nds {

[[nodiscard]] std::vector<u32> findNdsSdatOffsets(core::ByteReader reader);
[[nodiscard]] std::optional<NdsLayout> parseNdsLayout(core::ByteReader reader, u32 baseOffset);
[[nodiscard]] std::optional<NdsFileRange> ndsFileRange(core::ByteReader reader, const NdsLayout& layout, u16 fileId);
[[nodiscard]] NdsSequenceRange ndsSequenceRangeForFatEntry(core::ByteReader reader, u32 offset, u32 size);

}  // namespace vgmtrans::formats::nds

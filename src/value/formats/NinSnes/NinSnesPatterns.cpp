/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnesPatterns.h"

#include "value/scan/BytePattern.h"

#include <array>
#include <cstring>

namespace vgmtrans::formats::nin_snes {

Pattern::Pattern(const char* bytes, const char* mask, size_t size)
    : bytes_(reinterpret_cast<const u8*>(bytes), reinterpret_cast<const u8*>(bytes) + size), mask_(mask, size) {
}

std::optional<u32> Pattern::find(core::ByteReader reader) const {
  return core::findBytePattern(reader, core::MaskedBytePattern{bytes_, mask_});
}

#define NINSNES_BYTE_PATTERN Pattern
#define NINSNES_PATTERN_OWNER Patterns
#include "../../../shared/NinSnesScannerPatterns.inc"
#undef NINSNES_PATTERN_OWNER
#undef NINSNES_BYTE_PATTERN

}  // namespace vgmtrans::formats::nin_snes

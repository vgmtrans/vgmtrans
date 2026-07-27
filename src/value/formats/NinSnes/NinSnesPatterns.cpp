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

// Pilotwings:
//   mov a,$00 / cmp a,#$ff / beq / and a,#$1f / bne start-song
// $00-$03 are the canonical mirrors of input ports $F4-$F7.
Pattern Patterns::ptnReadSongRequestPort("\xe4\x00\x68\xff\xf0\x00\x28\x1f\xd0\x00", "x?xxx?xxx?", 10);

// Some HAL derivatives bypass the variable written by FA and apply a fixed
// percussion base directly in either note dispatch or the instrument loader.
//
// Kirby's Dream Land 3:
//   cmp a,#$ca / bcc / sbc a,#$a7 / call loader / mov y,#$a4
Pattern Patterns::ptnFixedPercussionBaseDispatch("\x68\xca\x90\x07\xa8\xa7\x3f\x11\x0b\x8d\xa4", "x?x?x?x??xx", 11);

// Vegas Stakes:
//   mov abs+x,a / mov y,a / bpl +3 / setc / sbc a,#$ca / mov y,#6 / mul ya
Pattern Patterns::ptnFixedPercussionBaseLoader("\xd5\x11\x02\xfd\x10\x03\x80\xa8\xca\x8d\x06\xcf", "x??xxxxx?xxx", 12);

std::optional<u8> detectFixedPercussionBase(core::ByteReader reader, u8 percussionMinimum) {
  if (const auto offset = Patterns::ptnFixedPercussionBaseDispatch.find(reader)) {
    const u8 detectedMinimum = reader.u8At(*offset + 1);
    const u8 subtract = reader.u8At(*offset + 5);
    if (detectedMinimum == percussionMinimum && subtract <= detectedMinimum) {
      return static_cast<u8>(detectedMinimum - subtract);
    }
  }
  if (const auto offset = Patterns::ptnFixedPercussionBaseLoader.find(reader)) {
    const u8 subtract = reader.u8At(*offset + 8);
    if (subtract <= percussionMinimum) {
      return static_cast<u8>(percussionMinimum - subtract);
    }
  }
  return std::nullopt;
}

}  // namespace vgmtrans::formats::nin_snes

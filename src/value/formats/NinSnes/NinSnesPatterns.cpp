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

// Super Mario World and Pilotwings:
//   setc / sbc a,#$d0 / mov y,#6 / mov $14,#<table / mov $15,#>table / call instrument-loader
Pattern Patterns::ptnEarlierPercussionTable("\x80\xa8\xd0\x8d\x06\x8f\x00\x14\x8f\x00\x15\x3f\x00\x00",
                                            "xxxxxx?xx?xx??", 14);

// Konami:
//   compare against percussion status, multiply the slot by three, then read
//   pan, volume reduction, and the note byte from a driver-resident table.
Pattern Patterns::ptnKonamiPercussionDispatch(
    "\xad\xca\x90\x00\x3f\x00\x00\xf5\x11\x02\x80\xa8\xca\xc4\x1e\x1c\x84\x1e\xfd\xe4\x00\x24\x47\xf0\x00\xf6\x00\x00",
    "xxx?x??xxxxxxxxxxxxx?xxx?x??", 28);

// Konami N-SPC derivatives disable the timers, set timer 0's target, then
// enable timer 0. Gradius III uses absolute register writes; Parodius Da! uses
// direct-page writes. Both schedulers advance the sequence once per timer 0
// result, so the immediate operand is the sequence tick duration in units of
// 125 microseconds.
Pattern Patterns::ptnKonamiTimer0Direct("\xe8\xf0\xc4\xf1\xe8\x00\xc4\xfa\xe8\x01\xc4\xf1", "xxxxx?xxxxxx", 12);
Pattern Patterns::ptnKonamiTimer0Absolute("\xe8\xf0\xc5\xf1\x00\xe8\x00\xc5\xfa\x00\xe8\x01\xc5\xf1\x00",
                                         "xxxxxx?xxxxxxxx", 15);

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

std::optional<u8> detectKonamiTempoTimerTarget(core::ByteReader reader) {
  if (const auto offset = Patterns::ptnKonamiTimer0Direct.find(reader)) {
    const u8 target = reader.u8At(*offset + 5);
    return target == 0 ? std::nullopt : std::optional{target};
  }
  if (const auto offset = Patterns::ptnKonamiTimer0Absolute.find(reader)) {
    const u8 target = reader.u8At(*offset + 6);
    return target == 0 ? std::nullopt : std::optional{target};
  }
  return std::nullopt;
}

}  // namespace vgmtrans::formats::nin_snes

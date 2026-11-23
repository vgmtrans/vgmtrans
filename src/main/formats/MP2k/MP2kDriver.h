/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

enum class MP2kDriverVariant {
  Standard,
  Bon,
  NintendoRd1,
  NintendoRd1TwoByteNotes,
};

constexpr bool isNintendoRd1Driver(MP2kDriverVariant variant) noexcept {
  return variant == MP2kDriverVariant::NintendoRd1 ||
         variant == MP2kDriverVariant::NintendoRd1TwoByteNotes;
}

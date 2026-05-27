/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "types.h"

enum class Endianness { Little, Big };
enum class Signedness { Signed, Unsigned };

inline u16 swap_bytes16(u16 val) {
  return (val << 8) | (val >> 8);
}

inline u32 swap_bytes32(u32 val) {
  return ((val << 24) | ((val & 0x0000FF00) << 8) | ((val & 0x00FF0000) >> 8) | (val >> 24));
}

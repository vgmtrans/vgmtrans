/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "Types.h"

struct SizeOffsetPair {
  u32 size;
  u32 offset;

  SizeOffsetPair() : size(0), offset(0) {}

  SizeOffsetPair(u32 offset_, u32 size_) : size(size_), offset(offset_) {}
};

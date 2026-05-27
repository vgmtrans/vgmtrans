/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"
#include "Format.h"
#include "FilegroupMatcher.h"
#include "ItikitiSnesScanner.h"

constexpr u8 kItikitiSnesSeqNoteKeyBias = 24;

BEGIN_FORMAT(ItikitiSnes)
  USING_SCANNER(ItikitiSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

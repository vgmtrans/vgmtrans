/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "ItikitiSnesScanner.h"

constexpr uint8_t kItikitiSnesSeqNoteKeyBias = 24;

BEGIN_FORMAT(ItikitiSnes)
  USING_SCANNER(ItikitiSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

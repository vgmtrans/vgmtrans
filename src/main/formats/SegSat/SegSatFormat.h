/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "Format.h"
#include "FilegroupMatcher.h"
#include "SegSatScanner.h"


// ************
// SegSatFormat
// ************

BEGIN_FORMAT(SegSat)
  USING_SCANNER(SegSatScanner)
  USING_MATCHER(FilegroupMatcher)
  USES_COLLECTION_FOR_SEQ_CONVERSION()
END_FORMAT()

/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Root.h"
#include "SegSatScanner.h"


// ************
// SegSatFormat
// ************

BEGIN_FORMAT(SegSat)
  USING_SCANNER(SegSatScanner)
END_FORMAT()

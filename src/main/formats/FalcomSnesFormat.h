/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "FalcomSnesScanner.h"


// ****************
// FalcomSnesFormat
// ****************

BEGIN_FORMAT(FalcomSnes)
  USING_SCANNER(FalcomSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum FalcomSnesVersion {
  FALCOMSNES_NONE = 0,  // Unknown Version
  FALCOMSNES_YS5,       // Ys V
};

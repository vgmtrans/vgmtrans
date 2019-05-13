/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "NamcoSnesScanner.h"


// *******************
// NamcoSnesFormat
// *******************

BEGIN_FORMAT(NamcoSnes)
  USING_SCANNER(NamcoSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum NamcoSnesVersion: uint8_t {
  NAMCOSNES_NONE = 0,              // Unknown Version
  NAMCOSNES_STANDARD,              // Wagyan Paradise etc.
};

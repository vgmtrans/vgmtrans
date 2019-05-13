/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "MoriSnesScanner.h"


// **************
// MoriSnesFormat
// **************

BEGIN_FORMAT(MoriSnes)
  USING_SCANNER(MoriSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum MoriSnesVersion: uint8_t {
  MORISNES_NONE = 0,              // Unknown Version
  MORISNES_STANDARD,              // Gokinjo Boukentai etc.
};

/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "RareSnesScanner.h"


// ***************
// RareSnesFormat
// ***************

BEGIN_FORMAT(RareSnes)
  USING_SCANNER(RareSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum RareSnesVersion {
  RARESNES_NONE = 0,  // Unknown Version
  RARESNES_DKC,       // Donkey Kong Country
  RARESNES_KI,        // Killer Instinct
  RARESNES_DKC2,      // Donkey Kong Country 2 (and DKC3)
  RARESNES_WNRN,      // Ken Griffey Jr. Winning Run
};

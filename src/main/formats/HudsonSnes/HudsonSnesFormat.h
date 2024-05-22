/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "HudsonSnesScanner.h"

// ****************
// HudsonSnesFormat
// ****************

BEGIN_FORMAT(HudsonSnes)
  USING_SCANNER(HudsonSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum HudsonSnesVersion: uint8_t {
  HUDSONSNES_NONE = 0,              // Unknown Version
  // Earlier Version:
  // Super Bomberman 2, Hagane
  HUDSONSNES_V0,
  // Version 1.xx:
  // Super Bomberman 3 (1.16)
  // Super Genjin 2 (1.16E)
  // Caravan Shooting Collection (1.17s)
  HUDSONSNES_V1,
  // Ver 2.xx
  // An American Tail: Fievel Goes West (No Version String)
  // Do-Re-Mi Fantasy (2.10)
  // Tengai Makyou Zero (2.27b)
  // Super Bomberman 4 (2.28)
  // Kishin Douji Zenki 3 (2.28)
  // Same Game (2.30a)
  // Super Bomberman 5 (2.32)
  // Bomberman B-Daman (2.32)
  HUDSONSNES_V2,
};

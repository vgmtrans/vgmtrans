#pragma once
#include "Format.h"
#include "Matcher.h"
#include "main/Core.h"
#include "SuzukiSnesScanner.h"


// **************
// SuzukiSnesFormat
// **************

BEGIN_FORMAT(SuzukiSnes)
  USING_SCANNER(SuzukiSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum SuzukiSnesVersion: uint8_t {
  SUZUKISNES_NONE = 0,              // Unknown Version
  SUZUKISNES_SD3,                   // Seiken Densetsu 3
  SUZUKISNES_BL,                    // Bahamut Lagoon
  SUZUKISNES_SMR,                   // Super Mario RPG (mostly identical to Bahamut Lagoon)
};

#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "SuzukiSnesScanner.h"


// **************
// SuzukiSnesFormat
// **************

BEGIN_FORMAT(SuzukiSnes)
USING_SCANNER(SuzukiSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum SuzukiSnesVersion
{
	SUZUKISNES_NONE = 0,              // Unknown Version
	SUZUKISNES_V1,                    // Seiken Densetsu 3
	SUZUKISNES_V2,                    // Bahamut Lagoon, Super Mario RPG
};

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

enum MoriSnesVersion
{
	MORISNES_NONE = 0,              // Unknown Version
	MORISNES_STANDARD,              // Gokinjo Boukentai etc.
};

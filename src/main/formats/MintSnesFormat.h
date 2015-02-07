#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "MintSnesScanner.h"


// **************
// MintSnesFormat
// **************

BEGIN_FORMAT(MintSnes)
USING_SCANNER(MintSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum MintSnesVersion
{
	MINTSNES_NONE = 0,              // Unknown Version
	MINTSNES_STANDARD,              // Gokinjo Boukentai etc.
};

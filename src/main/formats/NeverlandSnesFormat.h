#pragma once
#include "Format.h"
#include "Root.h"
#include "NeverlandSnesScanner.h"
#include "Matcher.h"
#include "VGMColl.h"

// *************
// NeverlandSnesFormat
// *************

BEGIN_FORMAT(NeverlandSnes)
	USING_SCANNER(NeverlandSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum NeverlandSnesVersion
{
	NEVERLANDSNES_NONE = 0,           // Not Supported
	NEVERLANDSNES_SFC,                // Lufia
	NEVERLANDSNES_S2C,                // Lufia II etc.
};

#pragma once
#include "Format.h"
#include "Root.h"
#include "NinSnesScanner.h"
#include "Matcher.h"
#include "VGMColl.h"

// *************
// NinSnesFormat
// *************

BEGIN_FORMAT(NinSnes)
	USING_SCANNER(NinSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum NinSnesVersion
{
	NINSNES_NONE = 0,           // Not Supported
	NINSNES_UNKNOWN,            // Unknown version (only header support)
	NINSNES_EARLIER,            // Eariler version (Super Mario World, Pilotwings)
	NINSNES_STANDARD,           // Common version with voice commands $e0-fa (e.g. Yoshi Island)
};

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

	// derived formats
	NINSNES_RD1,                // Nintendo RD1 (e.g. Super Metroid, Earthbound)
	NINSNES_RD2,                // Nintendo RD2 (e.g. Marvelous)
	NINSNES_KONAMI,             // Old Konami games (e.g. Gradius III, Castlevania IV, Legend of the Mystical Ninja)
	NINSNES_LEMMINGS,           // Lemmings
	NINSNES_INTELLI_FE3,        // Fire Emblem 3
	NINSNES_INTELLI_FE4,        // Fire Emblem 4
	NINSNES_HUMAN,              // Human games (e.g. Clock Tower, Firemen, Septentrion)
};

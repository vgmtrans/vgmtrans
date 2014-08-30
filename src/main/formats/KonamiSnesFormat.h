#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "KonamiSnesScanner.h"


// ***************
// KonamiSnesFormat
// ***************

BEGIN_FORMAT(KonamiSnes)
	USING_SCANNER(KonamiSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum KonamiSnesVersion : uint8_t
{
	KONAMISNES_NONE = 0,        // Unknown Version
	KONAMISNES_NORMAL_REV1,     // Ganbare Goemon 2, etc.
	KONAMISNES_NORMAL_REV2,     // Ganbare Goemon 3, etc.
	KONAMISNES_NORMAL_REV3,     // Animaniacs
};

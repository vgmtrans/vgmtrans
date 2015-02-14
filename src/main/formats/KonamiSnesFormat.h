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


enum KonamiSnesVersion
{
	KONAMISNES_NONE = 0,        // Unknown Version
	KONAMISNES_V1,              // Contra 3, etc.
	KONAMISNES_V2,              // Madara 2, etc.
	KONAMISNES_V3,              // Ganbare Goemon 2, etc.
	KONAMISNES_V4,              // Ganbare Goemon 3, etc.
	KONAMISNES_V5,              // Animaniacs
};

#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "ChunSnesScanner.h"


// **************
// ChunSnesFormat
// **************

BEGIN_FORMAT(ChunSnes)
USING_SCANNER(ChunSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum ChunSnesVersion
{
	CHUNSNES_NONE = 0,              // Unknown Version
	CHUNSNES_SUMMER_V2,             // Otogirisou
	CHUNSNES_WINTER_V1,             // Dragon Quest 5
	CHUNSNES_WINTER_V2,             // Torneco no Daibouken: Fushigi no Dungeon
	CHUNSNES_WINTER_V3,             // Kamaitachi no Yoru
};

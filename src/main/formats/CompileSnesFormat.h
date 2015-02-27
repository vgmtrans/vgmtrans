#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "CompileSnesScanner.h"


// *****************
// CompileSnesFormat
// *****************

BEGIN_FORMAT(CompileSnes)
	USING_SCANNER(CompileSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum CompileSnesVersion : uint8_t
{
	COMPILESNES_NONE = 0,       // Unknown Version
	COMPILESNES_ALESTE,         // Super Aleste (Space Megaforce)
	COMPILESNES_JAKICRUSH,      // Jaki Crush
	COMPILESNES_SUPERPUYO,      // Super Puyo Puyo
	COMPILESNES_STANDARD,       // Super Nazo Puyo (1995) and later
};

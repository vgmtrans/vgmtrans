#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "AkaoSnesScanner.h"


// **************
// AkaoSnesFormat
// **************

BEGIN_FORMAT(AkaoSnes)
USING_SCANNER(AkaoSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()


// AKAO SNES is used by a lot of games, but they have minor differences each other.
// Here I classify them into 4 versions roughly, and try to handle game-specific differences by minor version code.
enum AkaoSnesVersion
{
	AKAOSNES_NONE = 0,              // Unknown Version
	AKAOSNES_V1,                    // Final Fantasy 4
	AKAOSNES_V2,                    // Romancing SaGa
	AKAOSNES_V3,                    // Final Fantasy 5, etc.
	AKAOSNES_V4,                    // Romancing SaGa 2, Final Fantasy 6, etc.
};

enum AkaoSnesMinorVersion {
	AKAOSNES_NOMINORVERSION = 0,    // Unknown Minor Version
	AKAOSNES_V1_FF4,                // Final Fantasy 4
	AKAOSNES_V2_RS1,                // Romancing SaGa
	AKAOSNES_V3_FF5,                // Final Fantasy 5, Hanjuku Hero
	AKAOSNES_V3_SD2,                // Seiken Densetsu 2
	AKAOSNES_V3_FFMQ,               // Final Fantasy Mystic Quest
	AKAOSNES_V4_RS2,                // Romancing SaGa 2
	AKAOSNES_V4_FF6,                // Final Fantasy 6
	AKAOSNES_V4_LAL,                // Live A Live
	AKAOSNES_V4_FM,                 // Front Mission
	AKAOSNES_V4_CT,                 // Chrono Trigger
	AKAOSNES_V4_RS3,                // Romancing SaGa 3
	AKAOSNES_V4_GH,                 // Gun Hazard
	AKAOSNES_V4_BSGAME,             // BS Satellite games
};

#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "CapcomSnesScanner.h"


// ***************
// CapcomSnesFormat
// ***************

BEGIN_FORMAT(CapcomSnes)
	USING_SCANNER(CapcomSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum CapcomSnesVersion
{
	NONE = 0,                           // Unknown Version
	V1_BGM_IN_LIST,                     // U.N. Squadron, Super Ghouls 'N Ghosts, etc.
	V2_BGM_USUALLY_AT_FIXED_LOCATION,   // Aladdin, The Magical Quest Starring Mickey Mouse, Captain Commando, etc.
	V3_BGM_FIXED_LOCATION,              // Mega Man X, etc.
};

#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "RareSnesScanner.h"


// ***************
// RareSnesFormat
// ***************

BEGIN_FORMAT(RareSnes)
	USING_SCANNER(RareSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum RareSnesVersion
{
	NONE = 0,   // Unknown Version
	DKC,        // Donkey Kong Country
	KI,         // Killer Instinct
	DKC2,       // Donkey Kong Country 2 (and DKC3)
	WNRN,       // Ken Griffey Jr. Winning Run
};

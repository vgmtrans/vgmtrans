#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "PrismSnesScanner.h"


// ***************
// PrismSnesFormat
// ***************

BEGIN_FORMAT(PrismSnes)
	USING_SCANNER(PrismSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum PrismSnesVersion
{
	PRISMSNES_NONE = 0,  // Unknown Version
	PRISMSNES_STANDARD,  // Dual Orb 2
};

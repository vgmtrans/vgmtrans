#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "IMaxSnesScanner.h"


// ***************
// IMaxSnesFormat
// ***************

BEGIN_FORMAT(IMaxSnes)
	USING_SCANNER(IMaxSnesScanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum IMaxSnesVersion
{
	IMAXSNES_NONE = 0,  // Unknown Version
	IMAXSNES_STANDARD,  // Dual Orb 2
};

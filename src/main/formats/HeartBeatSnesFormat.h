#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "HeartBeatSnesScanner.h"


// *******************
// HeartBeatSnesFormat
// *******************

BEGIN_FORMAT(HeartBeatSnes)
USING_SCANNER(HeartBeatSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum HeartBeatSnesVersion
{
	HEARTBEATSNES_NONE = 0,              // Unknown Version
	HEARTBEATSNES_STANDARD,              // Dragon Quest 6 and 3
};

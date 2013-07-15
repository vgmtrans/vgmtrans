#pragma once
#include "Format.h"
#include "Root.h"
#include "NinSnesScanner.h"
#include "Matcher.h"
#include "VGMColl.h"

// *************
// NinSnesFormat
// *************

BEGIN_FORMAT(NinSnes)
	USING_SCANNER(NinSnesScanner)
	//USING_MATCHER(GetIdMatcher)
	//USING_MATCHER(SimpleMatcher)
END_FORMAT()



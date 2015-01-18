#pragma once
#include "Format.h"
#include "Root.h"
#include "SuzukiSnesScanner.h"


// ****************
// SuzukiSnesFormat
// ****************

BEGIN_FORMAT(SuzukiSnes)
	USING_SCANNER(SuzukiSnesScanner)
	//USING_MATCHER(SimpleMatcher)
	//USING_COLL(SquarePS2Coll)
END_FORMAT()

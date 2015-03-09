#pragma once
#include "Format.h"
#include "Root.h"
#include "SquarePS2Scanner.h"
#include "Matcher.h"
#include "VGMColl.h"

// ***************
// SquarePS2Format
// ***************

BEGIN_FORMAT(SquarePS2)
	USING_SCANNER(SquarePS2Scanner)
	USING_MATCHER(GetIdMatcher)
	//USING_MATCHER(SimpleMatcher)
END_FORMAT()



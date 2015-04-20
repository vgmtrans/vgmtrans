#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "TamSoftPS1Scanner.h"


// ****************
// TamSoftPS1Format
// ****************
#define TAMSOFTPS1_MAX_SONGS    51
#define TAMSOFTPS1_KEY_OFFSET   11

BEGIN_FORMAT(TamSoftPS1)
	USING_SCANNER(TamSoftPS1Scanner)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()

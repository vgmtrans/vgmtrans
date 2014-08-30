#pragma once
#include "../Format.h"
#include "../Root.h"
#include "../VGMColl.h"
#include "../Matcher.h"
#include "FFTScanner.h"

BEGIN_FORMAT(FFT)
	USING_SCANNER(FFTScanner)
	USING_MATCHER(GetIdMatcher)
END_FORMAT()

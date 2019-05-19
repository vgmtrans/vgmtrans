/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Root.h"
#include "VGMColl.h"
#include "Matcher.h"
#include "FFTScanner.h"

BEGIN_FORMAT(FFT)
USING_SCANNER(FFTScanner)
USING_MATCHER(GetIdMatcher)
END_FORMAT()

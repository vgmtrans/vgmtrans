/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
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

enum HeartBeatSnesVersion : uint8_t {
    HEARTBEATSNES_NONE = 0,  // Unknown Version
    HEARTBEATSNES_STANDARD,  // Dragon Quest 6 and 3
};

/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "GraphResSnesScanner.h"

// ***************
// GraphResSnesFormat
// ***************

BEGIN_FORMAT(GraphResSnes)
USING_SCANNER(GraphResSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum GraphResSnesVersion {
    GRAPHRESSNES_NONE = 0,  // Unknown Version
    GRAPHRESSNES_STANDARD,  // Mickey no Tokyo Disneyland Daibouken etc.
};

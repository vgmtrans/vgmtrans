#pragma once
#include "Format.h"
#include "FilegroupMatcher.h"
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

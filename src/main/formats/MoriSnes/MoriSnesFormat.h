#pragma once

#include "Types.h"
#include "Format.h"
#include "FilegroupMatcher.h"
#include "MoriSnesScanner.h"


// **************
// MoriSnesFormat
// **************

BEGIN_FORMAT(MoriSnes)
  USING_SCANNER(MoriSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum MoriSnesVersion: u8 {
  MORISNES_NONE = 0,              // Unknown Version
  MORISNES_STANDARD,              // Gokinjo Boukentai etc.
};

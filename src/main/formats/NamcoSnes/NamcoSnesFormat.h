#pragma once

#include "base/types.h"
#include "Format.h"
#include "FilegroupMatcher.h"
#include "NamcoSnesScanner.h"


// *******************
// NamcoSnesFormat
// *******************

BEGIN_FORMAT(NamcoSnes)
  USING_SCANNER(NamcoSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum NamcoSnesVersion: u8 {
  NAMCOSNES_NONE = 0,              // Unknown Version
  NAMCOSNES_STANDARD,              // Wagyan Paradise etc.
};

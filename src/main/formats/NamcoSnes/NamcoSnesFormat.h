#pragma once
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

enum NamcoSnesVersion: uint8_t {
  NAMCOSNES_NONE = 0,              // Unknown Version
  NAMCOSNES_V1,                    // Blue Crystal Rod
  NAMCOSNES_V2,                    // Wagyan Paradise
};

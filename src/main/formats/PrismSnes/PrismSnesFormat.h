#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "PrismSnesScanner.h"


// ***************
// PrismSnesFormat
// ***************

BEGIN_FORMAT(PrismSnes)
  USING_SCANNER(PrismSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum PrismSnesVersion {
  PRISMSNES_NONE = 0,  // Unknown Version
  PRISMSNES_CGV,       // Cosmo Gang: The Video
  PRISMSNES_DO,        // Dual Orb
  PRISMSNES_DO2,       // Dual Orb 2 (King of Dragons and Pieces too, they are actually different a little, though)
};

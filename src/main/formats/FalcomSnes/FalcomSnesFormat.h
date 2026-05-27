#pragma once
#include "FalcomSnesScanner.h"
#include "FilegroupMatcher.h"
#include "Format.h"

// ****************
// FalcomSnesFormat
// ****************

BEGIN_FORMAT(FalcomSnes)
  USING_SCANNER(FalcomSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum FalcomSnesVersion {
  FALCOMSNES_NONE = 0,  // Unknown Version
  FALCOMSNES_YS5,       // Ys V
};

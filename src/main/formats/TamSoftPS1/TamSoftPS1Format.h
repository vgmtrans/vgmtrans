#pragma once
#include "FilegroupMatcher.h"
#include "Format.h"
#include "TamSoftPS1Scanner.h"

// ****************
// TamSoftPS1Format
// ****************
#define TAMSOFTPS1_KEY_OFFSET   0

BEGIN_FORMAT(TamSoftPS1)
  USING_SCANNER(TamSoftPS1Scanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

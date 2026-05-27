#pragma once
#include "AsciiShuichiSnesScanner.h"
#include "FilegroupMatcher.h"
#include "Format.h"

// ***************
// AsciiShuichiSnesFormat
// ***************

BEGIN_FORMAT(AsciiShuichiSnes)
  USING_SCANNER(AsciiShuichiSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

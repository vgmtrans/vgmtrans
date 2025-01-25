#pragma once
#include "Format.h"
#include "FilegroupMatcher.h"
#include "AsciiShuichiSnesScanner.h"


// ***************
// AsciiShuichiSnesFormat
// ***************

BEGIN_FORMAT(AsciiShuichiSnes)
  USING_SCANNER(AsciiShuichiSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

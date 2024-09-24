#pragma once
#include "Format.h"
#include "SquarePS2Scanner.h"
#include "GetIdMatcher.h"

// ***************
// SquarePS2Format
// ***************

BEGIN_FORMAT(SquarePS2)
  USING_SCANNER(SquarePS2Scanner)
  USING_MATCHER(GetIdMatcher)
END_FORMAT()



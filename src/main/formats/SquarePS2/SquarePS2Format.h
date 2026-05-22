#pragma once
#include "Format.h"
#include "GetIdMatcher.h"
#include "SquarePS2Scanner.h"
// ***************
// SquarePS2Format
// ***************

BEGIN_FORMAT(SquarePS2)
  USING_SCANNER(SquarePS2Scanner)
  USING_MATCHER(GetIdMatcher)
END_FORMAT()



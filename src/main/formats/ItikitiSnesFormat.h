#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "ItikitiSnesScanner.h"

BEGIN_FORMAT(ItikitiSnes)
  USING_SCANNER(ItikitiSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

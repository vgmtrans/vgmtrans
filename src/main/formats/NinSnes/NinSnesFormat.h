#pragma once
#include "Format.h"
#include "NinSnesTypes.h"
#include "NinSnesScanner.h"
#include "FilegroupMatcher.h"

BEGIN_FORMAT(NinSnes)
USING_SCANNER(NinSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()

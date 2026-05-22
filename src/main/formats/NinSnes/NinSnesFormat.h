#pragma once
#include "FilegroupMatcher.h"
#include "Format.h"
#include "NinSnesScanner.h"
#include "NinSnesTypes.h"
BEGIN_FORMAT(NinSnes)
USING_SCANNER(NinSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()

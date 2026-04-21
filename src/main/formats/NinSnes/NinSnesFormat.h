#pragma once
#include "Format.h"
#include "NinSnesTypes.h"
#include "NinSnesScanner.h"
#include "FilegroupMatcher.h"

BEGIN_FORMAT(NinSnes)
  USING_SCANNER(NinSnesScanner)
  USING_MATCHER(FilegroupMatcher)

  static inline bool isQuintetVersion(NinSnesVersion version) {
    return version == NINSNES_QUINTET_ACTR ||
        version == NINSNES_QUINTET_ACTR2 ||
        version == NINSNES_QUINTET_IOG ||
        version == NINSNES_QUINTET_TS;
  }
END_FORMAT()

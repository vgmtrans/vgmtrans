#pragma once

#include "base/Types.h"
#include "FilegroupMatcher.h"
#include "Format.h"
#include "HeartBeatSnesScanner.h"

// *******************
// HeartBeatSnesFormat
// *******************

BEGIN_FORMAT(HeartBeatSnes)
  USING_SCANNER(HeartBeatSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum HeartBeatSnesVersion: u8 {
  HEARTBEATSNES_NONE = 0,              // Unknown Version
  HEARTBEATSNES_STANDARD,              // Dragon Quest 6 and 3
};

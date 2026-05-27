#pragma once

#include "Types.h"
#include "Format.h"
#include "FilegroupMatcher.h"
#include "CompileSnesScanner.h"


// *****************
// CompileSnesFormat
// *****************

BEGIN_FORMAT(CompileSnes)
  USING_SCANNER(CompileSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum CompileSnesVersion: u8 {
  COMPILESNES_NONE = 0,       // Unknown Version
  COMPILESNES_ALESTE,         // Super Aleste (Space Megaforce)
  COMPILESNES_JAKICRUSH,      // Jaki Crush
  COMPILESNES_SUPERPUYO,      // Super Puyo Puyo
  COMPILESNES_STANDARD,       // Super Nazo Puyo (1995) and later
};

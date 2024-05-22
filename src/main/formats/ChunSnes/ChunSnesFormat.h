#pragma once
#include "Format.h"
#include "Matcher.h"
#include "ChunSnesScanner.h"


// **************
// ChunSnesFormat
// **************

BEGIN_FORMAT(ChunSnes)
  USING_SCANNER(ChunSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum ChunSnesVersion: uint8_t {
  CHUNSNES_NONE = 0,              // Unknown Version
  CHUNSNES_SUMMER,                // Otogirisou
  CHUNSNES_WINTER,                // Dragon Quest 5 etc.
};

enum ChunSnesMinorVersion: uint8_t {
  CHUNSNES_NOMINORVERSION = 0,    // Unknown Minor Version
  CHUNSNES_SUMMER_V2,             // Otogirisou
  CHUNSNES_WINTER_V1,             // Dragon Quest 5
  CHUNSNES_WINTER_V2,             // Torneco no Daibouken: Fushigi no Dungeon
  CHUNSNES_WINTER_V3,             // Kamaitachi no Yoru
};

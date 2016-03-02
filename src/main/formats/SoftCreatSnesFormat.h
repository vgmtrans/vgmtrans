#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "SoftCreatSnesScanner.h"


// ***************
// SoftCreatSnesFormat
// ***************

BEGIN_FORMAT(SoftCreatSnes)
  USING_SCANNER(SoftCreatSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum SoftCreatSnesVersion {
  SOFTCREATSNES_NONE = 0,  // Unknown Version
  SOFTCREATSNES_V1,        // Spiderman and the X-Men: Arcade's Revenge, Equinox
  SOFTCREATSNES_V2,        // Plok!
  SOFTCREATSNES_V3,        // Spider-Man and Venom: Maximum Carnage
  SOFTCREATSNES_V4,        // The Tick, Ken Griffey Jr. Presents Major League Baseball
  SOFTCREATSNES_V5,        // Tin Star, Foreman for Real
};

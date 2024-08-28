#pragma once
#include "Format.h"
#include "FilegroupMatcher.h"
#include "CapcomSnesScanner.h"


// ***************
// CapcomSnesFormat
// ***************

BEGIN_FORMAT(CapcomSnes)
  USING_SCANNER(CapcomSnesScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()


enum CapcomSnesVersion: uint8_t {
  CAPCOMSNES_NONE = 0,                            // Unknown Version
  CAPCOMSNES_V1_BGM_IN_LIST,                      // U.N. Squadron, Super Ghouls 'N Ghosts, etc.
  CAPCOMSNES_V2_BGM_USUALLY_AT_FIXED_LOCATION,    // Aladdin, The Magical Quest Starring Mickey Mouse, Captain Commando, etc.
  CAPCOMSNES_V3_BGM_FIXED_LOCATION,               // Mega Man X, etc.
};

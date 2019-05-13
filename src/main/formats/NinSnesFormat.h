/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Root.h"
#include "NinSnesScanner.h"
#include "Matcher.h"
#include "VGMColl.h"

// *************
// NinSnesFormat
// *************

enum NinSnesVersion {
  NINSNES_NONE = 0,           // Not Supported
  NINSNES_UNKNOWN,            // Unknown version (only header support)
  NINSNES_EARLIER,            // Eariler version (Super Mario World, Pilotwings)
  NINSNES_STANDARD,           // Common version with voice commands $e0-fa (e.g. Yoshi Island)

                              // derived formats
  NINSNES_RD1,                // Nintendo RD1 (e.g. Super Metroid, Earthbound)
  NINSNES_RD2,                // Nintendo RD2 (e.g. Marvelous)
  NINSNES_HAL,                // HAL Laboratory games (e.g. Kirby series)
  NINSNES_KONAMI,             // Old Konami games (e.g. Gradius III, Castlevania IV, Legend of the Mystical Ninja)
  NINSNES_LEMMINGS,           // Lemmings
  NINSNES_INTELLI_FE3,        // Fire Emblem 3
  NINSNES_INTELLI_TA,         // Tetris Attack
  NINSNES_INTELLI_FE4,        // Fire Emblem 4
  NINSNES_HUMAN,              // Human games (e.g. Clock Tower, Firemen, Septentrion)
  NINSNES_TOSE,               // TOSE games (e.g. Yoshi's Safari, Dragon Ball Z: Super Butouden 2)
  NINSNES_QUINTET_ACTR,       // ActRaiser, Soul Blazer
  NINSNES_QUINTET_ACTR2,      // ActRaiser 2
  NINSNES_QUINTET_IOG,        // Illusion of Gaia, Robotrek
  NINSNES_QUINTET_TS,         // Terranigma (Tenchi Souzou)
  NINSNES_FALCOM_YS4,         // Ys IV
};

BEGIN_FORMAT(NinSnes)
  USING_SCANNER(NinSnesScanner)
  USING_MATCHER(FilegroupMatcher)

  static inline bool IsQuintetVersion(NinSnesVersion version) {
    return version == NINSNES_QUINTET_ACTR ||
        version == NINSNES_QUINTET_ACTR2 ||
        version == NINSNES_QUINTET_IOG ||
        version == NINSNES_QUINTET_TS;
  }
END_FORMAT()

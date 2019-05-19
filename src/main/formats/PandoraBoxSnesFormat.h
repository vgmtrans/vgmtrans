/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "PandoraBoxSnesScanner.h"

// ***************
// PandoraBoxSnesFormat
// ***************

BEGIN_FORMAT(PandoraBoxSnes)
USING_SCANNER(PandoraBoxSnesScanner)
USING_MATCHER(FilegroupMatcher)
END_FORMAT()

enum PandoraBoxSnesVersion {
    PANDORABOXSNES_NONE = 0,  // Unknown Version
    PANDORABOXSNES_V1,        // Kishin Korinden Oni etc.
    PANDORABOXSNES_V2,        // Traverse: Starlight and Prairie etc.
};

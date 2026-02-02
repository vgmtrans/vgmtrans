#pragma once
#include "Format.h"
#include "PSDSEScanner.h"
#include "VGMColl.h"

// *********
// PSDSEFormat
// *********
// DSE stands for Digital Sound Elements, a sound engine by Procyon Studios.
// Used in Pokemon Mystery Dungeon: Explorers of Sky and other games.

BEGIN_FORMAT(PSDSE)
USING_SCANNER(PSDSEScanner)
END_FORMAT()

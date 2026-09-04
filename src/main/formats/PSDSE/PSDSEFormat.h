#pragma once
#include "Format.h"
#include "PSDSEMatcher.h"
#include "PSDSEScanner.h"
#include "VGMColl.h"

// *********
// PSDSEFormat
// *********
// DSE stands for Digital Sound Elements, a sound engine by Procyon Studios.
// [Pokemon Mystery Dungeon: Explorers of Sky]: Audio uses Procyon Studio Digital Sound Elements containers.

BEGIN_FORMAT(PSDSE)
USING_SCANNER(PSDSEScanner)
USING_MATCHER(PSDSEMatcher)
END_FORMAT()

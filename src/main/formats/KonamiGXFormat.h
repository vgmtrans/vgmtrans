/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Root.h"
#include "VGMColl.h"
#include "KonamiGXScanner.h"

BEGIN_FORMAT(KonamiGX)
USING_SCANNER(KonamiGXScanner)
END_FORMAT()

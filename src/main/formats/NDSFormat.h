/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Root.h"
#include "NDSScanner.h"
#include "VGMColl.h"

// *********
// NDSFormat
// *********

BEGIN_FORMAT(NDS)
USING_SCANNER(NDSScanner)
END_FORMAT()

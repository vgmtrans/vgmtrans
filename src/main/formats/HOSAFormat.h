/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Root.h"
#include "VGMColl.h"
#include "HOSAScanner.h"

BEGIN_FORMAT(HOSA)
USING_SCANNER(HOSAScanner)
END_FORMAT()

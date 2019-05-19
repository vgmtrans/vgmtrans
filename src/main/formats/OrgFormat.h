/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "Root.h"
#include "OrgScanner.h"

// *********
// OrgFormat
// *********

BEGIN_FORMAT(Org)
USING_SCANNER(OrgScanner)
END_FORMAT()

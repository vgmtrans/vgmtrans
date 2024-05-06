/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Format.h"
#include "MP2kScanner.h"

BEGIN_FORMAT(MP2k)
  USING_SCANNER(MP2kScanner)
END_FORMAT()

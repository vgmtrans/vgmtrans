/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "TamSoftPS1Scanner.h"


// ****************
// TamSoftPS1Format
// ****************
#define TAMSOFTPS1_KEY_OFFSET   0

BEGIN_FORMAT(TamSoftPS1)
  USING_SCANNER(TamSoftPS1Scanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

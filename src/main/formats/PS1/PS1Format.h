/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "FilegroupMatcher.h"
#include "PS1Seq.h"
#include "formats/PS1/Vab.h"
#include "PS1SeqScanner.h"

// *********
// PS1Format
// *********

BEGIN_FORMAT(PS1)
  USING_SCANNER(PS1SeqScanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Format.h"
#include "FilegroupMatcher.h"
#include "KonamiPS1Scanner.h"

// ***************
// KonamiPS1Format
// ***************

BEGIN_FORMAT(KonamiPS1)
  USING_SCANNER(KonamiPS1Scanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

// [TODO] Use a matcher which can associate VAB properly.

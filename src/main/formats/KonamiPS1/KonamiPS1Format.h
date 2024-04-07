#pragma once
#include "Format.h"
#include "Matcher.h"
#include "Root.h"
#include "KonamiPS1Scanner.h"


// ***************
// KonamiPS1Format
// ***************

BEGIN_FORMAT(KonamiPS1)
  USING_SCANNER(KonamiPS1Scanner)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()

// [TODO] Use a matcher which can associate VAB properly.

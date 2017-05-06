#pragma once
#include "Format.h"
#include "main/Core.h"
#include "Matcher.h"
#include "VGMColl.h"
#include "PS1Seq.h"
#include "Vab.h"
#include "PS1SeqScanner.h"

// *********
// PS1Format
// *********

BEGIN_FORMAT(PS1)
  USING_SCANNER(PS1SeqScanner)
  //USING_MATCHER_WITH_ARG(SimpleMatcher, true)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()




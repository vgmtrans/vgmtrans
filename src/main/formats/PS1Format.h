/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Format.h"
#include "Root.h"
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




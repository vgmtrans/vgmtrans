#pragma once
#include "Format.h"
#include "Root.h"
#include "HeartBeatPS1Seq.h"
#include "Vab.h"
#include "HeartBeatPS1SeqScanner.h"
#include "Matcher.h"
#include "VGMColl.h"

// *********
// PS1Format
// *********

BEGIN_FORMAT(HeartBeatPS1)
	USING_SCANNER(HeartBeatPS1SeqScanner)
	//USING_MATCHER_WITH_ARG(SimpleMatcher, true)
	USING_MATCHER(FilegroupMatcher)
END_FORMAT()




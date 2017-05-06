#pragma once
#include "Format.h"
#include "main/Core.h"
#include "HeartBeatPS1Seq.h"
#include "Vab.h"
#include "HeartBeatPS1Scanner.h"
#include "Matcher.h"
#include "VGMColl.h"

#define HEARTBEATPS1_SND_HEADER_SIZE 0x3C

// ******************
// HeartBeatPS1Format
// ******************

BEGIN_FORMAT(HeartBeatPS1)
  USING_SCANNER(HeartBeatPS1Scanner)
  //USING_MATCHER_WITH_ARG(SimpleMatcher, true)
  USING_MATCHER(FilegroupMatcher)
END_FORMAT()




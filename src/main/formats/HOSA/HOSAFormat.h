#pragma once
#include "Format.h"
#include "Root.h"
#include "VGMColl.h"
#include "HOSAScanner.h"


BEGIN_FORMAT(HOSA)
  USING_SCANNER(HOSAScanner)
END_FORMAT()

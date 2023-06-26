#pragma once
#include "Format.h"
#include "Root.h"
#include "VGMColl.h"
#include "CPSScanner.h"


BEGIN_FORMAT(CPS)
  USING_SCANNER(CPSScanner)
END_FORMAT()

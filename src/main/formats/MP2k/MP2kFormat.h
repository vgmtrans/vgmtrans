#pragma once
#include "Format.h"
#include "Root.h"
#include "MP2kScanner.h"

BEGIN_FORMAT(MP2k)
  USING_SCANNER(MP2kScanner)
END_FORMAT()
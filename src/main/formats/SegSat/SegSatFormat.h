#pragma once
#include "Format.h"
#include "SegSatScanner.h"


// ************
// SegSatFormat
// ************

BEGIN_FORMAT(SegSat)
  USING_SCANNER(SegSatScanner)
END_FORMAT()

#pragma once
#include "Format.h"
#include "GetIdMatcher.h"
#include "FFTScanner.h"

BEGIN_FORMAT(FFT)
  USING_SCANNER(FFTScanner)
  USING_MATCHER(GetIdMatcher)
END_FORMAT()

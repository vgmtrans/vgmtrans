#pragma once
#include "FFTScanner.h"
#include "Format.h"
#include "GetIdMatcher.h"

BEGIN_FORMAT(FFT)
  USING_SCANNER(FFTScanner)
  USING_MATCHER(GetIdMatcher)
END_FORMAT()

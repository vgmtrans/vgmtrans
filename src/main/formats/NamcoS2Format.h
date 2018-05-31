#pragma once
#include "Format.h"
#include "main/Core.h"
#include "NamcoS2Scanner.h"


// *************
// NamcoS2Format
// *************

BEGIN_FORMAT(NamcoS2)
  USING_SCANNER(NamcoS2Scanner)
END_FORMAT()

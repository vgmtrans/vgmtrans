/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Format.h"
#include "KonamiTMNT2Scanner.h"

BEGIN_FORMAT(KonamiTMNT2)
  USING_SCANNER(KonamiTMNT2Scanner)
  USES_COLLECTION_FOR_SEQ_CONVERSION()
END_FORMAT()

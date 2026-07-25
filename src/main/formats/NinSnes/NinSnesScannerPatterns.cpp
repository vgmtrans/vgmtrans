/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/Types.h"
#include "NinSnesScanner.h"

#include <array>
#include <cstring>

#define NINSNES_BYTE_PATTERN BytePattern
#define NINSNES_PATTERN_OWNER NinSnesScanner
#include "NinSnesScannerPatterns.inc"
#undef NINSNES_PATTERN_OWNER
#undef NINSNES_BYTE_PATTERN

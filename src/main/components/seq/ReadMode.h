/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"

enum ReadMode : u8 {
  READMODE_ADD_TO_UI,
  READMODE_CONVERT_TO_MIDI,
  READMODE_FIND_DELTA_LENGTH
};

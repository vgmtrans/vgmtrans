/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "util/types.h"

#include <cstdint>

enum class BankSelectStyle {
  /* CC0 MSB (default) */
  GS,
  /* CC0 * 128 + CC32 */
  MMA
};

enum class SynthTarget : u8 {
  SoundFont,
  DLS,
};

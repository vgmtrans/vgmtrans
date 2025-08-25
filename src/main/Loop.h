/*
 * VGMTrans (c) - 2002-2025
 * Licensed under the zlib license
 * See the included LICENSE for more information
*/

#pragma once

#include "common.h"

enum LoopMeasure {
  LM_SAMPLES,
  LM_BYTES
};


struct Loop {
  int loopStatus       {-1};
  u32 loopType         {0};
  u8 loopStartMeasure  {LM_BYTES};
  u8 loopLengthMeasure {LM_BYTES};
  u32 loopStart        {0};
  u32 loopLength       {0};
};

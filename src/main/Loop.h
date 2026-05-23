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
  uint32_t loopType         {0};
  uint8_t loopStartMeasure  {LM_BYTES};
  uint8_t loopLengthMeasure {LM_BYTES};
  uint32_t loopStart        {0};
  uint32_t loopLength       {0};
};

/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once

enum LoopMeasure {
  LM_SAMPLES,
  LM_BYTES
};


struct Loop
{
  Loop()
      : loopStatus(-1), loopType(0), loopStartMeasure(LM_BYTES), loopLengthMeasure(LM_BYTES), loopStart(0),
        loopLength(0) { }

  int loopStatus;
  uint32_t loopType;
  uint8_t loopStartMeasure;
  uint8_t loopLengthMeasure;
  uint32_t loopStart;
  uint32_t loopLength;
};

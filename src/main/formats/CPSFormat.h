#pragma once
#include "Format.h"
#include "VGMColl.h"
#include "CPSScanner.h"

enum CPSSynth {
  QSOUND,
  OKIM6295,
  YM2151
};

constexpr int CPS1_OKIMSM6295_SAMPLE_RATE = 7576; // 1Mhz / 132, rounded

BEGIN_FORMAT(CPS)
  USING_SCANNER(CPSScanner)
END_FORMAT()

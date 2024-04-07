#pragma once
#include "Format.h"
#include "VGMColl.h"
#include "CPS2Scanner.h"
#include "CPS1Scanner.h"

enum CPSSynth {
  QSOUND,
  OKIM6295,
  YM2151
};

constexpr int CPS1_OKIMSM6295_SAMPLE_RATE = 7576; // 1Mhz / 132, rounded

BEGIN_FORMAT(CPS1)
  USING_SCANNER(CPS1Scanner)
END_FORMAT()

BEGIN_FORMAT(CPS2)
  USING_SCANNER(CPS2Scanner)
END_FORMAT()

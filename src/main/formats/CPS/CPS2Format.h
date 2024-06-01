#pragma once
#include "Format.h"
#include "CPS2Scanner.h"
#include "CPS1Scanner.h"

enum CPSSynth {
  QSOUND,
  OKIM6295,
  YM2151
};

constexpr int CPS1_OKIMSM6295_SAMPLE_RATE = 7576; // 1Mhz / 132, rounded
// The QSound interrupt clock is 250hz, but the main sound code runs only every 4th tick.
// (see the "and 3" instruction at 0xF9 in sfa2 code)
constexpr double CPS2_DRIVER_RATE_HZ = 250 / 4.0;
// In CPS3, sound driver iterations are triggered by the vblank interrupt (IRQ 12) which runs at 59.6hz
constexpr double CPS3_DRIVER_RATE_HZ = 59.599491;


BEGIN_FORMAT(CPS1)
  USING_SCANNER(CPS1Scanner)
END_FORMAT()

BEGIN_FORMAT(CPS2)
  USING_SCANNER(CPS2Scanner)
END_FORMAT()

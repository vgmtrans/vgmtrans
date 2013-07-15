#pragma once

#include "..\types.h"
#include "externals.h"

int SPUinit(void);
int SPUopen(void);
void SPUsetlength(s32 stop, s32 fade);
int SPUclose(void);
void SPUendflush(void);

// External, called by SPU code.
void SPUirq(void);


extern u16  regArea[0x200];
extern u16  spuMem[256*1024];
extern u8 * spuMemC;
extern u8 * pSpuIrq;
extern u8 * pSpuBuffer;

extern SPUCHAN s_chan[MAXCHAN+1];                     // channel + 1 infos (1 is security for fmod handling)
extern REVERBInfo rvb;

extern u32   dwNoiseVal;                          // global noise generator

extern u16  spuCtrl;                             // some vars to store psx reg infos
extern u16  spuStat;
extern u16  spuIrq;             
extern u32  spuAddr;                    // address into spu mem
extern int  bSPUIsOpen;
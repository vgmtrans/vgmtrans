/*  Pcsx - Pc Psx Emulator
 *  Copyright (C) 1999-2002  Pcsx Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __R3000A_H__
#define __R3000A_H__

#include <stdio.h>

#include "PsxCommon.h"

typedef struct {
	int  (*Init)();
	void (*Reset)();
	void (*Execute)(u32 cycles);		/* executes up to a break */
	void (*ExecuteBlock)();	/* executes up to a jump */
	void (*Clear)(u32 Addr, u32 Size);
	void (*Shutdown)();
} R3000Acpu;

extern R3000Acpu *psxCpu;
extern R3000Acpu psxInt;

typedef union {
        struct {
                u32 r0, at, v0, v1, a0, a1, a2, a3,
                                                t0, t1, t2, t3, t4, t5, t6, t7,
                                                s0, s1, s2, s3, s4, s5, s6, s7,
                                                t8, t9, k0, k1, gp, sp, s8, ra, lo, hi;
        } n; //PACKSTRUCT;
        u32 r[34]; /* Lo, Hi in r[33] and r[34] */
} psxGPRRegs;

typedef union {
	struct {
		u32	Index,     Random,    EntryLo0,  EntryLo1,
						Context,   PageMask,  Wired,     Reserved0,
						BadVAddr,  Count,     EntryHi,   Compare,
						Status,    Cause,     EPC,       PRid,
						Config,    LLAddr,    WatchLO,   WatchHI,
						XContext,  Reserved1, Reserved2, Reserved3,
						Reserved4, Reserved5, ECC,       CacheErr,
						TagLo,     TagHi,     ErrorEPC,  Reserved6;
	} n ;//PACKSTRUCT;
	u32 r[32];
} psxCP0Regs;

typedef struct {
	psxGPRRegs GPR;		/* General Purpose Registers */
	psxCP0Regs CP0;		/* Coprocessor0 Registers */
	u32 pc;				/* Program counter */
	u32 code;			/* The instruction */
	u32 cycle;
	u32 interrupt;
} psxRegisters;

extern psxRegisters psxRegs;

#define _i32(x) (s32)x
#define _u32(x) (u32)x

#define _i16(x) (s16)x
#define _u16(x) (u32)x

#define _i8(x) (s8)x
#define _u8(x) (u8)x

/**** R3000A Instruction Macros ****/
#define _PC_       psxRegs.pc       // The next PC to be executed

#define _Funct_  ((psxRegs.code      ) & 0x3F)  // The funct part of the instruction register 
#define _Rd_     ((psxRegs.code >> 11) & 0x1F)  // The rd part of the instruction register 
#define _Rt_     ((psxRegs.code >> 16) & 0x1F)  // The rt part of the instruction register 
#define _Rs_     ((psxRegs.code >> 21) & 0x1F)  // The rs part of the instruction register 
#define _Sa_     ((psxRegs.code >>  6) & 0x1F)  // The sa part of the instruction register
#define _Im_     ((u16)psxRegs.code) // The immediate part of the instruction register
#define _Target_ (psxRegs.code & 0x03ffffff)    // The target part of the instruction register

#define _Imm_	((s16)psxRegs.code) // sign-extended immediate
#define _ImmU_	(psxRegs.code&0xffff) // zero-extended immediate

#define _rRs_   psxRegs.GPR.r[_Rs_]   // Rs register
#define _rRt_   psxRegs.GPR.r[_Rt_]   // Rt register
#define _rRd_   psxRegs.GPR.r[_Rd_]   // Rd register
#define _rSa_   psxRegs.GPR.r[_Sa_]   // Sa register
#define _rFs_   psxRegs.CP0.r[_Rd_]   // Fs register

#define _c2dRs_ psxRegs.CP2D.r[_Rs_]  // Rs cop2 data register
#define _c2dRt_ psxRegs.CP2D.r[_Rt_]  // Rt cop2 data register
#define _c2dRd_ psxRegs.CP2D.r[_Rd_]  // Rd cop2 data register
#define _c2dSa_ psxRegs.CP2D.r[_Sa_]  // Sa cop2 data register

#define _rHi_   psxRegs.GPR.n.hi   // The HI register
#define _rLo_   psxRegs.GPR.n.lo   // The LO register

#define _JumpTarget_    ((_Target_ * 4) + (_PC_ & 0xf0000000))   // Calculates the target during a jump instruction
#define _BranchTarget_  ((s16)_Im_ * 4 + _PC_)                 // Calculates the target during a branch instruction

#define _SetLink(x)     psxRegs.GPR.r[x] = _PC_ + 4;       // Sets the return address in the link register

int  psxInit();
void psxReset();
void psxShutdown();
void psxException(u32 code, u32 bd);
void psxBranchTest();
void psxExecuteBios();

#endif /* __R3000A_H__ */

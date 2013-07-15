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
#include "stdafx.h"
#include <stdlib.h>

#include "PsxCommon.h"

static int branch;
static int branch2;
static u32 branchPC;

// These macros are used to assemble the repassembler functions

//        if(!PSXM(psxRegs.pc)) puts("Whoops");   
//	  Fix this...

//        printf("%08x ", psxRegs.pc);

#define execI() { \
	psxRegs.code = BFLIP32(PSXMu32(psxRegs.pc)); \
        	\
	psxRegs.pc+= 4; psxRegs.cycle++; \
	psxBSC[psxRegs.code >> 26](); \
}

// Subsets
extern void (*psxBSC[64])();
extern void (*psxSPC[64])();
extern void (*psxREG[32])();
extern void (*psxCP0[32])();

static void delayRead(int reg, u32 bpc) {
	u32 rold, rnew;

//	SysPrintf("delayRead at %x!\n", psxRegs.pc);

	rold = psxRegs.GPR.r[reg];
	psxBSC[psxRegs.code >> 26](); // branch delay load
	rnew = psxRegs.GPR.r[reg];

	psxRegs.pc = bpc;

	psxBranchTest();

	psxRegs.GPR.r[reg] = rold;
	execI(); // first branch opcode
	psxRegs.GPR.r[reg] = rnew;

	branch = 0;
}

static void delayWrite(int reg, u32 bpc) {

/*	SysPrintf("delayWrite at %x!\n", psxRegs.pc);

//	SysPrintf("%s\n", disR3000AF(psxRegs.code, psxRegs.pc-4));
//	SysPrintf("%s\n", disR3000AF(PSXMu32(bpc), bpc));*/

	// no changes from normal behavior

	psxBSC[psxRegs.code >> 26]();

	branch = 0;
	psxRegs.pc = bpc;

	psxBranchTest();
}

static void delayReadWrite(int reg, u32 bpc) {

//	SysPrintf("delayReadWrite at %x!\n", psxRegs.pc);

	// the branch delay load is skipped

	branch = 0;
	psxRegs.pc = bpc;

	psxBranchTest();
}

// this defines shall be used with the tmp 
// of the next func (instead of _Funct_...)
#define _tFunct_  ((tmp      ) & 0x3F)  // The funct part of the instruction register 
#define _tRd_     ((tmp >> 11) & 0x1F)  // The rd part of the instruction register 
#define _tRt_     ((tmp >> 16) & 0x1F)  // The rt part of the instruction register 
#define _tRs_     ((tmp >> 21) & 0x1F)  // The rs part of the instruction register 
#define _tSa_     ((tmp >>  6) & 0x1F)  // The sa part of the instruction register

static void psxDelayTest(u32 reg, u32 bpc) {
	u32 tmp;

	tmp = BFLIP32(PSXMu32(bpc));
	branch = 1;

	switch (tmp >> 26) {
		case 0x00: // SPECIAL
			switch (_tFunct_) {
				case 0x00: // SLL
					if (!tmp) break; // NOP
				case 0x02: case 0x03: // SRL/SRA
					if (_tRd_ == reg && _tRt_ == reg) {
						delayReadWrite(reg, bpc); return;
					} else if (_tRt_ == reg) {
						delayRead(reg, bpc); return;
					} else if (_tRd_ == reg) {
						delayWrite(reg, bpc); return;
					}
					break;

				case 0x08: // JR
					if (_tRs_ == reg) {
						delayRead(reg, bpc); return;
					}
					break;
				case 0x09: // JALR
					if (_tRd_ == reg && _tRs_ == reg) {
						delayReadWrite(reg, bpc); return;
					} else if (_tRs_ == reg) {
						delayRead(reg, bpc); return;
					} else if (_tRd_ == reg) {
						delayWrite(reg, bpc); return;
					}
					break;

				// SYSCALL/BREAK just a break;

				case 0x20: case 0x21: case 0x22: case 0x23:
				case 0x24: case 0x25: case 0x26: case 0x27: 
				case 0x2a: case 0x2b: // ADD/ADDU...
				case 0x04: case 0x06: case 0x07: // SLLV...
					if (_tRd_ == reg && (_tRt_ == reg || _tRs_ == reg)) {
						delayReadWrite(reg, bpc); return;
					} else if (_tRt_ == reg || _tRs_ == reg) {
						delayRead(reg, bpc); return;
					} else if (_tRd_ == reg) {
						delayWrite(reg, bpc); return;
					}
					break;

				case 0x10: case 0x12: // MFHI/MFLO
					if (_tRd_ == reg) {
						delayWrite(reg, bpc); return;
					}
					break;
				case 0x11: case 0x13: // MTHI/MTLO
					if (_tRs_ == reg) {
						delayRead(reg, bpc); return;
					}
					break;

				case 0x18: case 0x19:
				case 0x1a: case 0x1b: // MULT/DIV...
					if (_tRt_ == reg || _tRs_ == reg) {
						delayRead(reg, bpc); return;
					}
					break;
			}
			break;

		case 0x01: // REGIMM
			switch (_tRt_) {
				case 0x00: case 0x02:
				case 0x10: case 0x12: // BLTZ/BGEZ...
					if (_tRs_ == reg) {
						delayRead(reg, bpc); return;
					}
					break;
			}
			break;

		// J would be just a break;
		case 0x03: // JAL
			if (31 == reg) {
				delayWrite(reg, bpc); return;
			}
			break;

		case 0x04: case 0x05: // BEQ/BNE
			if (_tRs_ == reg || _tRt_ == reg) {
				delayRead(reg, bpc); return;
			}
			break;

		case 0x06: case 0x07: // BLEZ/BGTZ
			if (_tRs_ == reg) {
				delayRead(reg, bpc); return;
			}
			break;

		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: // ADDI/ADDIU...
			if (_tRt_ == reg && _tRs_ == reg) {
				delayReadWrite(reg, bpc); return;
			} else if (_tRs_ == reg) {
				delayRead(reg, bpc); return;
			} else if (_tRt_ == reg) {
				delayWrite(reg, bpc); return;
			}
			break;

		case 0x0f: // LUI
			if (_tRt_ == reg) {
				delayWrite(reg, bpc); return;
			}
			break;

		case 0x10: // COP0
			switch (_tFunct_) {
				case 0x00: // MFC0
					if (_tRt_ == reg) { delayWrite(reg, bpc); return; }
					break;
				case 0x02: // CFC0
					if (_tRt_ == reg) { delayWrite(reg, bpc); return; }
					break;
				case 0x04: // MTC0
					if (_tRt_ == reg) { delayRead(reg, bpc); return; }
					break;
				case 0x06: // CTC0
					if (_tRt_ == reg) { delayRead(reg, bpc); return; }
					break;
				// RFE just a break;
			}
			break;

		case 0x22: case 0x26: // LWL/LWR
			if (_tRt_ == reg) {
				delayReadWrite(reg, bpc); return;
			} else if (_tRs_ == reg) {
				delayRead(reg, bpc); return;
			}
			break;

		case 0x20: case 0x21: case 0x23:
		case 0x24: case 0x25: // LB/LH/LW/LBU/LHU
			if (_tRt_ == reg && _tRs_ == reg) {
				delayReadWrite(reg, bpc); return;
			} else if (_tRs_ == reg) {
				delayRead(reg, bpc); return;
			} else if (_tRt_ == reg) {
				delayWrite(reg, bpc); return;
			}
			break;

		case 0x28: case 0x29: case 0x2a:
		case 0x2b: case 0x2e: // SB/SH/SWL/SW/SWR
			if (_tRt_ == reg || _tRs_ == reg) {
				delayRead(reg, bpc); return;
			}
			break;

		case 0x32: case 0x3a: // LWC2/SWC2
			if (_tRs_ == reg) {
				delayRead(reg, bpc); return;
			}
			break;
	}
	psxBSC[psxRegs.code >> 26]();

	branch = 0;
	psxRegs.pc = bpc;

	psxBranchTest();
}

static void psxNULL(void);

static INLINE void doBranch(u32 tar) {
	u32 tmp;

	branch2 = branch = 1;
	branchPC = tar;

	psxRegs.code = BFLIP32(PSXMu32(psxRegs.pc));

	psxRegs.pc+= 4; psxRegs.cycle++;

	// check for load delay
	tmp = psxRegs.code >> 26;
	switch (tmp) {
		case 0x10: // COP0
			switch (_Rs_) {
				case 0x00: // MFC0
				case 0x02: // CFC0
					psxDelayTest(_Rt_, branchPC);
					return;
			}
			break;
		case 0x32: // LWC2
			psxDelayTest(_Rt_, branchPC);
			return;
		default:
			if (tmp >= 0x20 && tmp <= 0x26) { // LB/LH/LWL/LW/LBU/LHU/LWR
				psxDelayTest(_Rt_, branchPC);
				return;
			}
			break;
	}

	psxBSC[psxRegs.code >> 26]();

        if((psxRegs.pc-8)==branchPC && !(psxRegs.code>>26)) 
        {
	 //printf("%08x\n",psxRegs.code>>26);
	 CounterDeadLoopSkip();
	}
	branch = 0;
	psxRegs.pc = branchPC;

	psxBranchTest();
}

/*********************************************************
* Arithmetic with immediate operand                      *
* Format:  OP rt, rs, immediate                          *
*********************************************************/
static void psxADDI() 	{ if (!_Rt_) return; _rRt_ = _u32(_rRs_) + _Imm_ ; }		// Rt = Rs + Im 	(Exception on Integer Overflow)
static void psxADDIU() { if (!_Rt_) return; _rRt_ = _u32(_rRs_) + _Imm_ ; }		// Rt = Rs + Im
static void psxANDI() 	{ if (!_Rt_) return; _rRt_ = _u32(_rRs_) & _ImmU_; }		// Rt = Rs And Im
static void psxORI() 	{ if (!_Rt_) return; _rRt_ = _u32(_rRs_) | _ImmU_; }		// Rt = Rs Or  Im
static void psxXORI() 	{ if (!_Rt_) return; _rRt_ = _u32(_rRs_) ^ _ImmU_; }		// Rt = Rs Xor Im
static void psxSLTI() 	{ if (!_Rt_) return; _rRt_ = _i32(_rRs_) < _Imm_ ; }		// Rt = Rs < Im		(Signed)
static void psxSLTIU() { if (!_Rt_) return; _rRt_ = _u32(_rRs_) < ((u32)_ImmU_); }		// Rt = Rs < Im		(Unsigned)

/*********************************************************
* Register arithmetic                                    *
* Format:  OP rd, rs, rt                                 *
*********************************************************/
static void psxADD()	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) + _u32(_rRt_); }	// Rd = Rs + Rt		(Exception on Integer Overflow)
static void psxADDU() 	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) + _u32(_rRt_); }	// Rd = Rs + Rt
static void psxSUB() 	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) - _u32(_rRt_); }	// Rd = Rs - Rt		(Exception on Integer Overflow)
static void psxSUBU() 	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) - _u32(_rRt_); }	// Rd = Rs - Rt
static void psxAND() 	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) & _u32(_rRt_); }	// Rd = Rs And Rt
static void psxOR() 	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) | _u32(_rRt_); }	// Rd = Rs Or  Rt
static void psxXOR() 	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) ^ _u32(_rRt_); }	// Rd = Rs Xor Rt
static void psxNOR() 	{ if (!_Rd_) return; _rRd_ =~(_u32(_rRs_) | _u32(_rRt_)); }// Rd = Rs Nor Rt
static void psxSLT() 	{ if (!_Rd_) return; _rRd_ = _i32(_rRs_) < _i32(_rRt_); }	// Rd = Rs < Rt		(Signed)
static void psxSLTU() 	{ if (!_Rd_) return; _rRd_ = _u32(_rRs_) < _u32(_rRt_); }	// Rd = Rs < Rt		(Unsigned)

/*********************************************************
* Register mult/div & Register trap logic                *
* Format:  OP rs, rt                                     *
*********************************************************/
static void psxDIV() {
	if (_i32(_rRt_) != 0) {
		_rLo_ = _i32(_rRs_) / _i32(_rRt_);
		_rHi_ = _i32(_rRs_) % _i32(_rRt_);
	}
}

static void psxDIVU() {
	if (_rRt_ != 0) {
		_rLo_ = _rRs_ / _rRt_;
		_rHi_ = _rRs_ % _rRt_;
	}
}

static void psxMULT() {
	u64 res = (s64)((s64)_i32(_rRs_) * (s64)_i32(_rRt_));

	psxRegs.GPR.n.lo = (u32)(res & 0xffffffff);
	psxRegs.GPR.n.hi = (u32)((res >> 32) & 0xffffffff);
}

static void psxMULTU() {
	u64 res = (u64)((u64)_u32(_rRs_) * (u64)_u32(_rRt_));

	psxRegs.GPR.n.lo = (u32)(res & 0xffffffff);
	psxRegs.GPR.n.hi = (u32)((res >> 32) & 0xffffffff);
}

/*********************************************************
* Register branch logic                                  *
* Format:  OP rs, offset                                 *
*********************************************************/
#define RepZBranchi32(op)      if(_i32(_rRs_) op 0) doBranch(_BranchTarget_);
#define RepZBranchLinki32(op)  if(_i32(_rRs_) op 0) { _SetLink(31); doBranch(_BranchTarget_); }

static void psxBGEZ()   { RepZBranchi32(>=) }      // Branch if Rs >= 0
static void psxBGEZAL() { RepZBranchLinki32(>=) }  // Branch if Rs >= 0 and link
static void psxBGTZ()   { RepZBranchi32(>) }       // Branch if Rs >  0
static void psxBLEZ()   { RepZBranchi32(<=) }      // Branch if Rs <= 0
static void psxBLTZ()   { RepZBranchi32(<) }       // Branch if Rs <  0
static void psxBLTZAL() { RepZBranchLinki32(<) }   // Branch if Rs <  0 and link

/*********************************************************
* Shift arithmetic with constant shift                   *
* Format:  OP rd, rt, sa                                 *
*********************************************************/
static void psxSLL() { if (!_Rd_) return; _rRd_ = _u32(_rRt_) << _Sa_; } // Rd = Rt << sa
static void psxSRA() { if (!_Rd_) return; _rRd_ = _i32(_rRt_) >> _Sa_; } // Rd = Rt >> sa (arithmetic)
static void psxSRL() { if (!_Rd_) return; _rRd_ = _u32(_rRt_) >> _Sa_; } // Rd = Rt >> sa (logical)

/*********************************************************
* Shift arithmetic with variant register shift           *
* Format:  OP rd, rt, rs                                 *
*********************************************************/
static void psxSLLV() { if (!_Rd_) return; _rRd_ = _u32(_rRt_) << _u32(_rRs_); } // Rd = Rt << rs
static void psxSRAV() { if (!_Rd_) return; _rRd_ = _i32(_rRt_) >> _u32(_rRs_); } // Rd = Rt >> rs (arithmetic)
static void psxSRLV() { if (!_Rd_) return; _rRd_ = _u32(_rRt_) >> _u32(_rRs_); } // Rd = Rt >> rs (logical)

/*********************************************************
* Load higher 16 bits of the first word in GPR with imm  *
* Format:  OP rt, immediate                              *
*********************************************************/
static void psxLUI() { if (!_Rt_) return; _rRt_ = psxRegs.code << 16; } // Upper halfword of Rt = Im

/*********************************************************
* Move from HI/LO to GPR                                 *
* Format:  OP rd                                         *
*********************************************************/
static void psxMFHI() { if (!_Rd_) return; _rRd_ = _rHi_; } // Rd = Hi
static void psxMFLO() { if (!_Rd_) return; _rRd_ = _rLo_; } // Rd = Lo

/*********************************************************
* Move to GPR to HI/LO & Register jump                   *
* Format:  OP rs                                         *
*********************************************************/
static void psxMTHI() { _rHi_ = _rRs_; } // Hi = Rs
static void psxMTLO() { _rLo_ = _rRs_; } // Lo = Rs

/*********************************************************
* Special purpose instructions                           *
* Format:  OP                                            *
*********************************************************/
static void psxBREAK() {
	// Break exception - psx rom doens't handles this
}

static void psxSYSCALL() {
	psxRegs.pc -= 4;
	psxException(0x20, branch);
}

static void psxRFE() {
	psxRegs.CP0.n.Status = (psxRegs.CP0.n.Status & 0xfffffff0) |
						  ((psxRegs.CP0.n.Status & 0x3c) >> 2);
}

/*********************************************************
* Register branch logic                                  *
* Format:  OP rs, rt, offset                             *
*********************************************************/
#define RepBranchi32(op)      if(_i32(_rRs_) op _i32(_rRt_)) { doBranch(_BranchTarget_); }

static void psxBEQ() {	RepBranchi32(==) }  // Branch if Rs == Rt
static void psxBNE() {	RepBranchi32(!=) }  // Branch if Rs != Rt

/*********************************************************
* Jump to target                                         *
* Format:  OP target                                     *
*********************************************************/
static void psxJ()   { doBranch(_JumpTarget_); }
static void psxJAL() {	_SetLink(31); doBranch(_JumpTarget_); }

/*********************************************************
* Register jump                                          *
* Format:  OP rs, rd                                     *
*********************************************************/
static void psxJR()   {                 doBranch(_u32(_rRs_)); }
static void psxJALR() { if (_Rd_) { _SetLink(_Rd_); } doBranch(_u32(_rRs_)); }

/*********************************************************
* Load and store for GPR                                 *
* Format:  OP rt, offset(base)                           *
*********************************************************/

#define _oB_ (_u32(_rRs_) + _Imm_)

static void psxLB() {
	if (_Rt_) {
		_rRt_ = (s8)psxMemRead8(_oB_); 
	} else {
		psxMemRead8(_oB_); 
	}
}

static void psxLBU() {
	if (_Rt_) {
		_rRt_ = psxMemRead8(_oB_);
	} else {
		psxMemRead8(_oB_); 
	}
}

static void psxLH() {
	if (_Rt_) {
		_rRt_ = (s16)psxMemRead16(_oB_);
	} else {
		psxMemRead16(_oB_);
	}
}

static void psxLHU() {
	if (_Rt_) {
		_rRt_ = psxMemRead16(_oB_);
	} else {
		psxMemRead16(_oB_);
	}
}

static void psxLW() {
	if (_Rt_) {
		_rRt_ = psxMemRead32(_oB_);
	} else {
		psxMemRead32(_oB_);
	}
}

static u32 LWL_MASK[4] = { 0xffffff, 0xffff, 0xff, 0 };
static u32 LWL_SHIFT[4] = { 24, 16, 8, 0 };

static void psxLWL() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	if (!_Rt_) return;
	_rRt_ =	( _u32(_rRt_) & LWL_MASK[shift]) | 
					( mem << LWL_SHIFT[shift]);

	/*
	Mem = 1234.  Reg = abcd

	0   4bcd   (mem << 24) | (reg & 0x00ffffff)
	1   34cd   (mem << 16) | (reg & 0x0000ffff)
	2   234d   (mem <<  8) | (reg & 0x000000ff)
	3   1234   (mem      ) | (reg & 0x00000000)
	*/
}

static u32 LWR_MASK[4] = { 0, 0xff000000, 0xffff0000, 0xffffff00 };
static u32 LWR_SHIFT[4] = { 0, 8, 16, 24 };

static void psxLWR() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	if (!_Rt_) return;
	_rRt_ =	( _u32(_rRt_) & LWR_MASK[shift]) | 
					( mem >> LWR_SHIFT[shift]);

	/*
	Mem = 1234.  Reg = abcd

	0   1234   (mem      ) | (reg & 0x00000000)
	1   a123   (mem >>  8) | (reg & 0xff000000)
	2   ab12   (mem >> 16) | (reg & 0xffff0000)
	3   abc1   (mem >> 24) | (reg & 0xffffff00)
	*/
}

static void psxSB() { psxMemWrite8 (_oB_, _u8 (_rRt_)); }
static void psxSH() { psxMemWrite16(_oB_, _u16(_rRt_)); }
static void psxSW() { psxMemWrite32(_oB_, _u32(_rRt_)); }

static const u32 SWL_MASK[4] = { 0xffffff00, 0xffff0000, 0xff000000, 0 };
static const u32 SWL_SHIFT[4] = { 24, 16, 8, 0 };

static void psxSWL() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	psxMemWrite32(addr & ~3,  (_u32(_rRt_) >> SWL_SHIFT[shift]) |
			     (  mem & SWL_MASK[shift]) );
	/*
	Mem = 1234.  Reg = abcd

	0   123a   (reg >> 24) | (mem & 0xffffff00)
	1   12ab   (reg >> 16) | (mem & 0xffff0000)
	2   1abc   (reg >>  8) | (mem & 0xff000000)
	3   abcd   (reg      ) | (mem & 0x00000000)
	*/
}

static const u32 SWR_MASK[4] = { 0, 0xff, 0xffff, 0xffffff };
static const u32 SWR_SHIFT[4] = { 0, 8, 16, 24 };

static void psxSWR() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	psxMemWrite32(addr & ~3,  (_u32(_rRt_) << SWR_SHIFT[shift]) |
			     (  mem & SWR_MASK[shift]) );

	/*
	Mem = 1234.  Reg = abcd

	0   abcd   (reg      ) | (mem & 0x00000000)
	1   bcd4   (reg <<  8) | (mem & 0x000000ff)
	2   cd34   (reg << 16) | (mem & 0x0000ffff)
	3   d234   (reg << 24) | (mem & 0x00ffffff)
	*/
}

/*********************************************************
* Moves between GPR and COPx                             *
* Format:  OP rt, fs                                     *
*********************************************************/
static void psxMFC0() { if (!_Rt_) return; _rRt_ = (int)_rFs_; }
static void psxCFC0() { if (!_Rt_) return; _rRt_ = (int)_rFs_; }

static INLINE void MTC0(int reg, u32 val) {
	switch (reg) {
		case 13: // Cause
			psxRegs.CP0.n.Cause = val & ~(0xfc00);

			// the next code is untested, if u know please
			// tell me if it works ok or not (linuzappz)
			if (psxRegs.CP0.n.Cause & psxRegs.CP0.n.Status & 0x0300 &&
				psxRegs.CP0.n.Status & 0x1) {
				psxException(psxRegs.CP0.n.Cause, 0);
			}
			break;

		default:
			psxRegs.CP0.r[reg] = val;
			break;
	}
}

static void psxMTC0() { MTC0(_Rd_, _u32(_rRt_)); }
static void psxCTC0() { MTC0(_Rd_, _u32(_rRt_)); }

/*********************************************************
* Unknow instruction (would generate an exception)       *
* Format:  ?                                             *
*********************************************************/
static void psxNULL() { 
#ifdef PSXCPU_LOG
	PSXCPU_LOG("psx: Unimplemented op %x\n", psxRegs.code);
#endif
}

static void psxSPECIAL() {
	psxSPC[_Funct_]();
}

static void psxREGIMM() {
	psxREG[_Rt_]();
}

static void psxCOP0() {
	psxCP0[_Rs_]();
}

static void psxHLE() {
	psxHLEt[psxRegs.code & 0xff]();
}

//static void (*psxBSC[64])() = {
static void (*psxBSC[64])() = {
	psxSPECIAL, psxREGIMM, psxJ   , psxJAL  , psxBEQ , psxBNE , psxBLEZ, psxBGTZ,
	psxADDI   , psxADDIU , psxSLTI, psxSLTIU, psxANDI, psxORI , psxXORI, psxLUI ,
	psxCOP0   , psxNULL  , psxNULL, psxNULL , psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL   , psxNULL  , psxNULL, psxNULL , psxNULL, psxNULL, psxNULL, psxNULL,
	psxLB     , psxLH    , psxLWL , psxLW   , psxLBU , psxLHU , psxLWR , psxNULL,
	psxSB     , psxSH    , psxSWL , psxSW   , psxNULL, psxNULL, psxSWR , psxNULL, 
	psxNULL   , psxNULL  , psxNULL, psxNULL , psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL   , psxNULL  , psxNULL, psxHLE  , psxNULL, psxNULL, psxNULL, psxNULL 
};


static void (*psxSPC[64])() = {
	psxSLL , psxNULL , psxSRL , psxSRA , psxSLLV   , psxNULL , psxSRLV, psxSRAV,
	psxJR  , psxJALR , psxNULL, psxNULL, psxSYSCALL, psxBREAK, psxNULL, psxNULL,
	psxMFHI, psxMTHI , psxMFLO, psxMTLO, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxMULT, psxMULTU, psxDIV , psxDIVU, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxADD , psxADDU , psxSUB , psxSUBU, psxAND    , psxOR   , psxXOR , psxNOR ,
	psxNULL, psxNULL , psxSLT , psxSLTU, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxNULL, psxNULL , psxNULL, psxNULL, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxNULL, psxNULL , psxNULL, psxNULL, psxNULL   , psxNULL , psxNULL, psxNULL
};

static void (*psxREG[32])() = {
	psxBLTZ  , psxBGEZ  , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL  , psxNULL  , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxBLTZAL, psxBGEZAL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL  , psxNULL  , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL
};

static void (*psxCP0[32])() = {
	psxMFC0, psxNULL, psxCFC0, psxNULL, psxMTC0, psxNULL, psxCTC0, psxNULL,
	psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxRFE , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL
};


///////////////////////////////////////////

static int intInit() {
	return 0;
}

static void intReset() {
 branch=branch2=0;
}

static void intExecute(u32 cycles) {
	for (int i=0; i<cycles; i++) 
	{
	 //if(!CounterSPURun()) 
	 //{
	 // psxShutdown();
	 // return;
	 //}
	 SPUendflush();
	 execI();
	}
}

static void intExecuteBlock() {
	branch2 = 0;
	while (!branch2) execI();
}

static void intClear(u32 Addr, u32 Size) {
}

static void intShutdown() {
}

R3000Acpu psxInt = {
	intInit,
	intReset,
	intExecute,
	intExecuteBlock,
	intClear,
	intShutdown
};

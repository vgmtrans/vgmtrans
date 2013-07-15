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

#include <stdio.h>
#include <string.h>

#include "PsxCommon.h"

#define HW_DMA4_MADR (psxHu32(0x10c0)) // SPU DMA
#define HW_DMA4_BCR  (psxHu32(0x10c4))
#define HW_DMA4_CHCR (psxHu32(0x10c8))

#define HW_DMA_PCR   (psxHu32(0x10f0))
#define HW_DMA_ICR   (psxHu32(0x10f4))

void psxHwReset() {
	memset(psxH, 0, 0x10000);
	psxRcntInit();
}

u8 psxHwRead8(u32 add) {
	u8 hard;

	switch (add) {
		default:
			hard = psxHu8(add); 
			return hard;
	}
	return hard;
}

u16 psxHwRead16(u32 add) {
	u16 hard;

	switch (add) {
                case 0x1f801100:
                        hard = psxRcntRcount(0);
                        return hard;
                case 0x1f801104:
                        hard = psxCounters[0].mode;
                        return hard;
                case 0x1f801108:
                        hard = psxCounters[0].target;
                        return hard;
                case 0x1f801110:
                        hard = psxRcntRcount(1);
                        return hard;
                case 0x1f801114:
                        hard = psxCounters[1].mode;
                        return hard;
                case 0x1f801118:
                        hard = psxCounters[1].target;
                        return hard;
		case 0x1f801120:
			hard = psxRcntRcount(2);
			return hard;
		case 0x1f801124:
			hard = psxCounters[2].mode;
			return hard;
		case 0x1f801128:
			hard = psxCounters[2].target;
			return hard;
	
		default:
			if (add>=0x1f801c00 && add<0x1f801e00) {
            		  hard = SPUreadRegister(add);
			} else {
				hard = BFLIP16(psxHu16(add)); 
			}
            return hard;
	}
	return hard;
}

u32 psxHwRead32(u32 add) {
	u32 hard;

	switch (add) {
		// time for rootcounters :)
                case 0x1f801100:
                        hard = psxRcntRcount(0);
                        return hard;
                case 0x1f801104:
                        hard = psxCounters[0].mode;
                        return hard;
                case 0x1f801108:
                        hard = psxCounters[0].target;
                        return hard;
               case 0x1f801110:
                        hard = psxRcntRcount(1);
                        return hard;
                case 0x1f801114:
                        hard = psxCounters[1].mode;
                        return hard;
                case 0x1f801118:
                        hard = psxCounters[1].target;
                        return hard;
		case 0x1f801120:
			hard = psxRcntRcount(2);
			return hard;
		case 0x1f801124:
			hard = psxCounters[2].mode;
			return hard;
		case 0x1f801128:
			hard = psxCounters[2].target;
			return hard;

		default:
			hard = BFLIP32(psxHu32(add)); 
			return hard;
	}
	return hard;
}

void psxHwWrite8(u32 add, u8 value) {
	switch (add) {
		default:
			psxHu8(add) = value;
			return;
	}
	psxHu8(add) = value;
}

void psxHwWrite16(u32 add, u16 value) {
	switch (add) {

		case 0x1f801070: 
			psxHu16(0x1070) &= BFLIP16( BFLIP16(psxHu16(0x1074)) & value);
			return;
                case 0x1f801100:
                        psxRcntWcount(0, value); return;
                case 0x1f801104:
                        psxRcntWmode(0, value); return;
                case 0x1f801108:
                        psxRcntWtarget(0, value); return;
                
                case 0x1f801110:
                        psxRcntWcount(1, value); return;
                case 0x1f801114:
                        psxRcntWmode(1, value); return;
                case 0x1f801118:
                        psxRcntWtarget(1, value); return;
        
                case 0x1f801120:
                        psxRcntWcount(2, value); return;
                case 0x1f801124:
                        psxRcntWmode(2, value); return;
                case 0x1f801128:
                        psxRcntWtarget(2, value); return;

		default:
			if (add>=0x1f801c00 && add<0x1f801e00) {
            			SPUwriteRegister(add, value);
				return;
			}

			psxHu16(add) = BFLIP16(value);
			return;
	}
	psxHu16(add) = BFLIP16(value);
}

#define	DMA_INTERRUPT(n) \
	if (BFLIP32(HW_DMA_ICR) & (1 << (16 + n))) { \
		HW_DMA_ICR|= BFLIP32(1 << (24 + n)); \
		psxHu32(0x1070) |= BFLIP32(8); \
	}

#define DmaExec(n) { \
	if (BFLIP32(HW_DMA##n##_CHCR) & 0x01000000 && BFLIP32(HW_DMA_PCR) & (8 << (n * 4))) { \
		psxDma##n(BFLIP32(HW_DMA##n##_MADR), BFLIP32(HW_DMA##n##_BCR), BFLIP32(HW_DMA##n##_CHCR)); \
		HW_DMA##n##_CHCR &= BFLIP32(~0x01000000); \
		DMA_INTERRUPT(n); \
	} \
}

void psxHwWrite32(u32 add, u32 value) {
	switch (add) {
		case 0x1f801070: 
			psxHu32(0x1070) &= BFLIP32(BFLIP32(psxHu32(0x1074)) & value);
			return;
		case 0x1f8010c8:
			HW_DMA4_CHCR = BFLIP32(value);        // DMA4 chcr (SPU DMA)
			DmaExec(4);
			return;
		case 0x1f8010f4:
		{
			u32 tmp = (~value) & BFLIP32(HW_DMA_ICR);
			HW_DMA_ICR = BFLIP32(((tmp ^ value) & 0xffffff) ^ tmp);
			return;
		}

                case 0x1f801100:
                        psxRcntWcount(0, value & 0xffff); return;
                case 0x1f801104:
                        psxRcntWmode(0, value); return;
                case 0x1f801108:
                        psxRcntWtarget(0, value & 0xffff); return; 
                                //  HW_DMA_ICR&= (~value)&0xff000000;
                
                case 0x1f801110:
                        psxRcntWcount(1, value & 0xffff); return;
                case 0x1f801114:
                        psxRcntWmode(1, value); return;
                case 0x1f801118:
                        psxRcntWtarget(1, value & 0xffff); return;

		case 0x1f801120:
			psxRcntWcount(2, value & 0xffff); return;
		case 0x1f801124:
			psxRcntWmode(2, value); return;
		case 0x1f801128:
			psxRcntWtarget(2, value & 0xffff); return;

		default:
			psxHu32(add) = BFLIP32(value);
			return;
	}
	psxHu32(add) = BFLIP32(value);
}


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

#ifndef __PSXMEMORY_H__
#define __PSXMEMORY_H__

#include "types.h"

#ifdef MSB_FIRST
static INLINE u16 BFLIP16(u16 x)
{
 return( ((x>>8)&0xFF)| ((x&0xFF)<<8) );
}

static INLINE u32 BFLIP32(u32 x)
{
 return ( ((x>>24)&0xFF) | ((x>>8)&0xFF00) | ((x<<8)&0xFF0000) | ((x<<24)&0xFF000000) );
}
#else
static INLINE u16 BFLIP16(u16 x)
{
 return x;
}

static INLINE u32 BFLIP32(u32 x)
{
 return x;
}
#endif

extern s8 *psxM;
#define psxMu32(mem)	(*(u32*)&psxM[(mem) & 0x1fffff])

extern s8 *psxP;
extern s8 *psxR;
#define psxRu32(mem)	(*(u32*)&psxR[(mem) & 0x7ffff])

extern s8 *psxH;

#define psxHu8(mem)	(*(u8*) &psxH[(mem) & 0xffff])

#define psxHu16(mem)   	(*(u16*)&psxH[(mem) & 0xffff])
#define psxHu32(mem)   	(*(u32*)&psxH[(mem) & 0xffff])

extern char **psxMemLUT;

#define PSXM(mem)		(psxMemLUT[(mem) >> 16] == 0 ? NULL : (void*)(psxMemLUT[(mem) >> 16] + ((mem) & 0xffff)))

#define PSXMu8(mem)	(*(u8 *)PSXM(mem))
#define PSXMu32(mem)    (*(u32*)PSXM(mem))

#define PSXMuR8(mem)        (PSXM(mem)?PSXMu8(mem):0)
#define PSXMuW8(mem,val)    (PSXM(mem)?PSXMu8(mem)=val:;)

int  psxMemInit();
void psxMemReset();
void psxMemShutdown();

u8   psxMemRead8 (u32 mem);
u16  psxMemRead16(u32 mem);
u32  psxMemRead32(u32 mem);
void psxMemWrite8 (u32 mem, u8 value);
void psxMemWrite16(u32 mem, u16 value);
void psxMemWrite32(u32 mem, u32 value);

void LoadPSXMem(u32 address, s32 length, char *data);

#endif /* __PSXMEMORY_H__ */

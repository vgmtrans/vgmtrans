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

#ifndef __PSXCOUNTERS_H__
#define __PSXCOUNTERS_H__

typedef struct {
	u32 count, mode, target;
	u32 sCycle, Cycle, rate, interrupt;
} psxCounter;

extern psxCounter psxCounters[5];

extern u32 psxNextCounter;
extern u32 psxNextsCounter;

void psxRcntInit();
void psxRcntUpdate();
void psxRcntWcount(u32 index, u32 value);
void psxRcntWmode(u32 index, u32 value);
void psxRcntWtarget(u32 index, u32 value);
u32 psxRcntRcount(u32 index);

void psxUpdateVSyncRate();

int CounterSPURun(void);
void CounterDeadLoopSkip();

#endif /* __PSXCOUNTERS_H__ */

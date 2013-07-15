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

#ifndef __PSXHW_H__
#define __PSXHW_H__

void psxHwReset();
u8   psxHwRead8 (u32 add);
u16  psxHwRead16(u32 add);
u32  psxHwRead32(u32 add);
void psxHwWrite8 (u32 add, u8  value);
void psxHwWrite16(u32 add, u16 value);
void psxHwWrite32(u32 add, u32 value);

#endif /* __PSXHW_H__ */

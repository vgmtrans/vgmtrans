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

#ifndef __PSXCOMMON_H__
#define __PSXCOMMON_H__

#if PSS_STYLE==2

#define PSS "\\"
#define PS '\\'

#elif PSS_STYLE==1

#define PSS "/"
#define PS '/'

#elif PSS_STYLE==3

#define PSS "\\"
#define PS '\\'
#endif

#include <zlib.h>

#include <sys/types.h>
#include "types.h"

void __Log(char *fmt, ...);

#define BIAS	2
#define PSXCLK	33868800	/* 33.8688 Mhz */

#include "R3000A.h"
#include "PsxMem.h"
#include "PsxHw.h"
#include "PsxBios.h"
#include "PsxDma.h"
#include "PsxCounters.h"
#include "PsxHLE.h"
#include "Spu.h"
#include "Misc.h"
#include "spu/spu.h"

#endif /* __PSXCOMMON_H__ */

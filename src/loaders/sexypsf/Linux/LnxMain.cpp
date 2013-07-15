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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "driver.h"
#include "Linux.h"

int main(int argc, char *argv[]) {
	PSFINFO *pi;

	SetupSound();
	if(!(pi=sexy_load(argv[1])))
	{
	 puts("Error loading PSF");
	 return(-1);
	}

	printf("Game:\t%s\nTitle:\t%s\nArtist:\t%s\nYear:\t%s\nGenre:\t%s\nPSF By:\t%s\nCopyright:\t%s\n",
		pi->game,pi->title,pi->artist,pi->year,pi->genre,pi->psfby,pi->copyright);
	{
	 PSFTAG *recur=pi->tags;
 	 while(recur)
	 {
	  printf("%s:\t%s\n",recur->key,recur->value);
	  recur=recur->next;
	 }
	}
	sexy_execute();
	return 0;
}

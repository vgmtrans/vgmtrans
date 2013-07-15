/***************************************************************************
                            oss.c  -  description
                             -------------------
    begin                : Wed May 15 2002
    copyright            : (C) 2002 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>
#include <sys/soundcard.h>

//*************************************************************************//
// History of changes:
//
// 2003/03/01 - linuzappz
// - added checkings for oss_audio_fd == -1
//
// 2003/02/08 - linuzappz
// - added iDisStereo stuff
//
// 2002/05/15 - Pete
// - generic cleanup for the Peops release
//
//*************************************************************************//

#define _IN_OSS

#ifndef _WINDOWS

////////////////////////////////////////////////////////////////////////
// oss globals
////////////////////////////////////////////////////////////////////////

#define OSS_MEM_DEF
#include "oss.h"
static int oss_audio_fd = -1;
extern int errno;

////////////////////////////////////////////////////////////////////////
// SETUP SOUND
////////////////////////////////////////////////////////////////////////

void SetupSound(void)
{
 int pspeed=44100;
 int pstereo;
 int format;
 int fragsize = 0;
 int myfrag;
 int oss_speed, oss_stereo;

 pstereo=OSS_MODE_STEREO;

 oss_speed = pspeed;
 oss_stereo = pstereo;

 if((oss_audio_fd=open("/dev/dsp",O_WRONLY,0))==-1)
  {
   printf("Sound device not available!\n");
   return;
  }

 if(ioctl(oss_audio_fd,SNDCTL_DSP_RESET,0)==-1)
  {
   printf("Sound reset failed\n");
   return;
  }

 // we use 64 fragments with 1024 bytes each
 /*
 fragsize=10;
 myfrag=(63<<16)|fragsize;

 if(ioctl(oss_audio_fd,SNDCTL_DSP_SETFRAGMENT,&myfrag)==-1)
  {
   printf("Sound set fragment failed!\n");
   return;        
  }
 */
 format = AFMT_S16_LE;

 if(ioctl(oss_audio_fd,SNDCTL_DSP_SETFMT,&format) == -1)
  {
   printf("Sound format not supported!\n");
   return;
  }

 if(format!=AFMT_S16_LE)
  {
   printf("Sound format not supported!\n");
   return;
  }

 if(ioctl(oss_audio_fd,SNDCTL_DSP_STEREO,&oss_stereo)==-1)
  {
   printf("Stereo mode not supported!\n");
   return;
  }

 if(ioctl(oss_audio_fd,SNDCTL_DSP_SPEED,&oss_speed)==-1)
  {
   printf("Sound frequency not supported\n");
   return;
  }

 if(oss_speed!=pspeed)
  {
   printf("Sound frequency not supported\n");
   return;
  }
}

////////////////////////////////////////////////////////////////////////
// REMOVE SOUND
////////////////////////////////////////////////////////////////////////

void RemoveSound(void)
{
 if(oss_audio_fd != -1 )
  {
   close(oss_audio_fd);
   oss_audio_fd = -1;
  }
}

////////////////////////////////////////////////////////////////////////
// FEED SOUND DATA
////////////////////////////////////////////////////////////////////////
#ifdef TIMEO
static long counter=0;
#endif

void sexyd_update(unsigned char* pSound,long lBytes)
{
 int check;

 if(oss_audio_fd == -1) return;
 #ifndef TIMEO
 write(oss_audio_fd,pSound,lBytes);
 #else
 counter+=lBytes;
 if(counter>=44100*4*16) sexy_stop();
 #endif
 if(!ioctl(fileno(stdin),FIONREAD,&check))
  if(check)
  {
   char buf[256];
   fgets(buf,256,stdin);
   if(buf[0]=='q') sexy_stop();
  }
}

#endif

/*  sexyPSF - PSF1 player
 *  Copyright (C) 2002-2004 xodnizel
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <xmms/plugin.h>
#include <xmms/util.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "driver.h"

#define CMD_SEEK	0x80000000
#define CMD_STOP	0x40000000

#define uint32 u32
#define int16 short

static volatile uint32 command;
static volatile int playing=0;

static PSFINFO *PSFInfo=NULL;

extern InputPlugin sexy_ip;
static char *fnsave=NULL;

InputPlugin *get_iplugin_info(void)
      {
         sexy_ip.description = "sexyPSF PSF1 Player "SPSFVERSION;
         return &sexy_ip;
      }

void init(void)
{
}
void about(void)
{

}
void config(void)
{

}

int testfile(char *fn)
{
 char buf[4];
 char *tmps;
 static const char *teststr="psflib";
 FILE *fp;
 if(!strncasecmp(fn,"http://",7)) return(0); // Don't handle http://blahblah

 /* Filter out psflib files */
 if(strlen(teststr) < strlen(fn))
 {
  tmps=fn+strlen(fn);
  tmps-=strlen(teststr);
  if(!strcasecmp(tmps,teststr))
   return(0);
 }

 if(!(fp=fopen(fn,"rb"))) return(0);
 if(fread(buf,1,4,fp)!=4) {fclose(fp);return(0);}
 fclose(fp);
 if(memcmp(buf,"PSF\x01",4))	// Only allow for PSF1 files for now.
   return 0;
 return(1);
}

static char *MakeTitle(PSFINFO *info)
{
 char *ret;

 ret=malloc( (info->game?strlen(info->game):0) + (info->title?strlen(info->title):0) + 3 + 1);
 sprintf(ret,"%s - %s",info->game?info->game:"",info->title?info->title:"");

 return(ret);
}

static void SI(void)
{
 char *tmp;
 tmp=MakeTitle(PSFInfo);
 sexy_ip.set_info(tmp,PSFInfo->length,44100*2*2*8,44100,2);
 free(tmp);
}

static pthread_t dethread;
void sexyd_update(unsigned char *Buffer, long count)
{
 int mask = ~((((16 / 8) * 2)) - 1);
 if(count)
  sexy_ip.add_vis_pcm(sexy_ip.output->written_time(), FMT_S16_NE, 2, count/4, Buffer);

 while(count>0)
 {
  int t=sexy_ip.output->buffer_free() & mask;
  if(t>count)
  {
   sexy_ip.output->write_audio(Buffer,count);
  }
  else
  {
   if(t)
    sexy_ip.output->write_audio(Buffer,t);
   usleep((count-t)*1000*5/441/2);
  }
  count-=t;
  Buffer+=t;
 }
 if(command&CMD_SEEK)
 {
  int t=(command&~(CMD_SEEK|CMD_STOP))*1000;

  if(sexy_seek(t))
   sexy_ip.output->flush(t);
  else	// Negative time!  Must make a C time machine.
  {
   sexy_stop();
   return;
  }
  command&=~CMD_SEEK;
 }
 if(command&CMD_STOP)
 {
  sexy_stop();
 }
}
static volatile int nextsong=0;

static void *playloop(void *arg)
{
 dofunky:

 sexy_execute();

 /* We have reached the end of the song. Now what... */
 sexy_ip.output->buffer_free();
 sexy_ip.output->buffer_free();

 while(!(command&CMD_STOP)) 
 {
  if(command&CMD_SEEK)
  {
   int t=(command&~(CMD_SEEK|CMD_STOP))*1000;
   sexy_ip.output->flush(t);
   if(!(PSFInfo=sexy_load(fnsave)))
    break;
   sexy_seek(t); 
   command&=~CMD_SEEK;
   goto dofunky;
  }
  if(!sexy_ip.output->buffer_playing()) break;
  usleep(2000);
 }
 sexy_ip.output->close_audio();
 if(!(command&CMD_STOP)) nextsong=1;
 pthread_exit(0);
}

static int paused;
static void play(char *fn)
{
 if(playing)
  return;
 nextsong=0;
 paused = 0;
 if(!sexy_ip.output->open_audio(FMT_S16_NE, 44100, 2))
 {
  puts("Error opening audio.");
  return;
 }
 fnsave=malloc(strlen(fn)+1);
 strcpy(fnsave,fn);
 if(!(PSFInfo=sexy_load(fn)))
 {
  sexy_ip.output->close_audio();
  nextsong=1;
 }
 else
 {
  command=0;
  SI();
  playing=1;
  pthread_create(&dethread,0,playloop,0);
 }
}

static void stop(void)
{
 if(!playing) return;

 if(paused)
  sexy_ip.output->pause(0);
 paused = 0;

 command=CMD_STOP;
 pthread_join(dethread,0);
 playing = 0;

 if(fnsave)
 {
  free(fnsave);
  fnsave=NULL;
 } 
 sexy_freepsfinfo(PSFInfo);
 PSFInfo=NULL;
}

static void sexy_pause(short p)
{
 if(!playing) return;
 sexy_ip.output->pause(p);
 paused = p;
}

static void seek(int time)
{
 if(!playing) return;
 command=CMD_SEEK|time;
}

static int gettime(void)
{
 if(nextsong)
  return(-1);
 if(!playing) return(0);
 return sexy_ip.output->output_time();
}

static void getsonginfo(char *fn, char **title, int *length)
{
  PSFINFO *tmp;

  if((tmp=sexy_getpsfinfo(fn)))
  {
   *length=tmp->length;
   *title=MakeTitle(tmp);
   sexy_freepsfinfo(tmp);
  }
}

InputPlugin sexy_ip =
{
 0,0,"Plays PSF1 files.",0,0,0,testfile,0,play,stop,sexy_pause,
 seek,0,gettime,0,0,0,0,0,0,0,getsonginfo,0,0
};

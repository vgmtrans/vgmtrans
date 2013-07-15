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
#include <stdlib.h>
#include <stdarg.h>

#include "PsxCommon.h"
#include "driver.h"

// LOAD STUFF

typedef struct {
    unsigned char id[8];
    u32 text;                   
    u32 data;                    
    u32 pc0;
    u32 gp0;                     
    u32 t_addr;
    u32 t_size;
    u32 d_addr;                  
    u32 d_size;                  
    u32 b_addr;                  
    u32 b_size;                  
    u32 S_addr;//normal must a s not a S but error (???)
    u32 s_size;
    u32 SavedSP;
    u32 SavedFP;
    u32 SavedGP;
    u32 SavedRA;
    u32 SavedS0;
} /*PACKSTRUCT*/ EXE_HEADER;

static long TimeToMS(const char *str)
{
             int x,c=0;
             int acc=0;
	     char s[100];

	     strncpy(s,str,100);
	     s[99]=0;

             for(x=strlen(s);x>=0;x--)
              if(s[x]=='.' || s[x]==',')
              {
               acc=atoi(s+x+1);
               s[x]=0;
              }
              else if(s[x]==':')
              {
               if(c==0) acc+=atoi(s+x+1)*10;
               else if(c==1) acc+=atoi(s+x+(x?1:0))*10*60;
               c++;
               s[x]=0;
              }
              else if(x==0)
	      {
               if(c==0) acc+=atoi(s+x)*10;
               else if(c==1) acc+=atoi(s+x)*10*60;
               else if(c==2) acc+=atoi(s+x)*10*60*60;
	      }
             acc*=100;  // To milliseconds.
	    return(acc);
}

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile)
{
  static wchar_t *ret;
  wchar_t *tp1;
                       
 #if PSS_STYLE==1
     tp1=((wchar_t *)wcsrchr (f,'/'));
 #else
     tp1=((wchar_t *)wcsrchr (f,'\\'));
  #if PSS_STYLE!=3
  {
     wchar_t *tp3;

     tp3=((wchar_t *)wcsrchr (f,'/'));
     if(tp1<tp3) tp1=tp3;
  }
  #endif
 #endif
     if(!tp1)
     {
      ret=(wchar_t*)malloc(wcslen(newfile)+1);
      wcscpy(ret,newfile);
     }
     else              
     {
      ret=(wchar_t*)malloc((tp1-f+2+wcslen(newfile)) * sizeof(wchar_t));	// 1(NULL), 1(/).
      memcpy(ret,f,(tp1-f) * sizeof(wchar_t));
      ret[tp1-f]=L'/';
      ret[tp1-f+1]=0;
      wcscat(ret,newfile);
     }
    return(ret);
}

static int GetKeyVal(char *buf, char **key, char **val)
{
 char *tmp;

 tmp=buf;

 /* First, convert any weirdo ASCII characters to spaces. */
 while(*tmp++) if(*tmp>0 && *tmp<0x20) *tmp=0x20;

 /* Strip off white space off end of string(which should be the "value"). */
 for(tmp=buf+strlen(buf)-1;tmp>=buf;tmp--)
 {
  if(*tmp != 0x20) break;
  *tmp=0;
 }

 /* Now, search for the first non-whitespace character. */
 while(*buf == 0x20) buf++; 

 tmp=buf;
 while((*buf != 0x20) && (*buf != '=')) 
 {
  if(!*buf) return(0);	/* Null character. */
  buf++;
 }

 /* Allocate memory, copy string, and terminate string. */
 if(!(*key=(char *)malloc(buf-tmp+1))) return(0);
 strncpy(*key,tmp,buf-tmp);
 (*key)[(buf-tmp)]=0;

 /* Search for "=" character. */
 while(*buf != '=')
 {
  if(!*buf) return(0);  /* Null character. */
  buf++;
 }

 buf++;	/* Skip over equals character. */

 /* Remove leading whitespace on value. */
 while(*buf == 0x20)
 {
  if(!*buf) return(0);  /* Null character. */
  buf++;
 }

 /* Allocate memory, and copy string over.  Trailing whitespace was eliminated
    earlier.
 */

 if(!(*val=(char*)malloc(strlen(buf)+1))) return(0);
 strcpy(*val,buf);

 //puts(*key);
 //puts(*val);

 return(1);
}

static void FreeTags(PSFTAG *tags)
{
 while(tags)
 {
  PSFTAG *tmp=tags->next;

  free(tags->key);
  free(tags->value);
  free(tags);

  tags=tmp;
 }
}

static void AddKV(PSFTAG **tag, char *key, char *val)
{
 PSFTAG *tmp;

 tmp=(PSFTAG*)malloc(sizeof(PSFTAG));
 memset(tmp,0,sizeof(PSFTAG));

 tmp->key=key;
 tmp->value=val;
 tmp->next=0;

 if(!*tag) *tag=tmp;
 else 
 {
  PSFTAG *rec;
  rec=*tag;
  while(rec->next) rec=rec->next;
  rec->next=tmp;
 }

}

typedef struct {
	int num;
	char *value;
} LIBNCACHE;

static int ccomp(const void *v1, const void *v2)
{
 const LIBNCACHE *a1,*a2;
 a1=(LIBNCACHE*)v1; a2=(LIBNCACHE*)v2;

 return(a1->num - a2->num);
}

static PSFINFO *LoadPSF(const wchar_t *path, int level, int type) // Type==1 for just info load.
{
        FILE *fp;
        EXE_HEADER tmpHead;
        char *in,*out=0;
	u8 head[4];
        u32 reserved;
        u32 complen;
        u32 crc32; 
        uLongf outlen;
	PSFINFO *psfi;
	PSFINFO *tmpi;

	//printf("Loading: %s\n",path);
        if(!(fp=_wfopen(path,L"rb")))
 	{
	 return(0);
	}
 
	fread(head,1,4,fp);
	if(memcmp(head,"PSF\x01",4)) return(0);

	psfi=(PSFINFO*)malloc(sizeof(PSFINFO));
	memset(psfi,0,sizeof(PSFINFO));
        psfi->stop=~0;
        psfi->fade=0; 

        fread(&reserved,1,4,fp);
        fread(&complen,1,4,fp);
	complen=BFLIP32(complen);

        fread(&crc32,1,4,fp);
	crc32=BFLIP32(crc32);

        fseek(fp,reserved,SEEK_CUR);

        if(type)
	 fseek(fp,complen,SEEK_CUR);
        else
        {
         in=(char*)malloc(complen);
         out=(char*)malloc(1024*1024*2+0x800);
         fread(in,1,complen,fp);
         outlen=1024*1024*2;
         uncompress((Bytef*)out,&outlen,(Bytef*)in,complen);
         free(in);
         memcpy(&tmpHead,out,sizeof(EXE_HEADER));
         psxRegs.pc = BFLIP32(tmpHead.pc0);
         psxRegs.GPR.n.gp = BFLIP32(tmpHead.gp0);
         psxRegs.GPR.n.sp = BFLIP32(tmpHead.S_addr);
         if (psxRegs.GPR.n.sp == 0) psxRegs.GPR.n.sp = 0x801fff00;

 	 if(level)
 	 {
	  LoadPSXMem(BFLIP32(tmpHead.t_addr),BFLIP32(tmpHead.t_size),out+0x800);
          free(out);
	 }
        }

        {
         u8 tagdata[5];
         if(fread(tagdata,1,5,fp)==5)
         {
          if(!memcmp(tagdata,"[TAG]",5))
          {
           char linebuf[1024];

           while(fgets(linebuf,1024,fp)>0)
           {
            int x;
	    char *key=0,*value=0;

	    if(!GetKeyVal(linebuf,&key,&value))
	    { 
	     if(key) free(key);
	     if(value) free(value);
	     continue;
            }

	    AddKV(&psfi->tags,key,value);

	    if(!level)
	    {
       	     static char *yoinks[8]={"title","artist","game","year","genre",
                                  "copyright","psfby","comment"};
	     char **yoinks2[8]={&psfi->title,&psfi->artist,&psfi->game,&psfi->year,&psfi->genre,
                                    &psfi->copyright,&psfi->psfby,&psfi->comment};
	     for(x=0;x<8;x++)
	      if(!_stricmp(key,yoinks[x]))
		*yoinks2[x]=value;
	     if(!_stricmp(key,"length"))
	      psfi->stop=TimeToMS(value);
	     else if(!_stricmp(key,"fade"))
	      psfi->fade=TimeToMS(value);
	    }

	    if(!_stricmp(key,"_lib") && !type)
	    {
	     wchar_t *tmpfn;
	     /* Load file name "value" from the directory specified in
		the full path(directory + file name) "path"
	     */

		 wchar_t tempfn[_MAX_PATH];
		 mbstowcs(tempfn, value, _MAX_PATH);

	     tmpfn=GetFileWithBase(path,tempfn);
	     if(!(tmpi=LoadPSF(tmpfn,level+1,0))) 
	     {
	      free(key);
	      free(value);
	      free(tmpfn);
 	      if(!level) free(out);
	      fclose(fp);
	      FreeTags(psfi->tags);
	      free(psfi);
	      return(0);
	     }
	     FreeTags(tmpi->tags);
	     free(tmpi);
	     free(tmpfn);
	    }
           }
          }
         }
        }  

        fclose(fp);

	/* Now, if we're at level 0(main PSF), load the main executable, and any libN stuff */
        if(!level && !type)
        {
         LoadPSXMem(BFLIP32(tmpHead.t_addr),BFLIP32(tmpHead.t_size),out+0x800);
	 free(out);
        }

	if(!type)	/* Load libN */
	{
	 LIBNCACHE *cache;
	 PSFTAG *tag;
	 unsigned int libncount=0;
	 unsigned int cur=0;

	 tag=psfi->tags;
	 while(tag)
	 {
	  //if(!_stricmp(tag->key,"_lib",4) && tag->key[4])
		 if(!_stricmp(tag->key,"_lib") && tag->key[4])
	   libncount++;
	  tag=tag->next;
	 }

	 if(libncount)
	 {
	  cache=(LIBNCACHE*)malloc(sizeof(LIBNCACHE)*libncount);

	  tag=psfi->tags;
	  while(tag)
	  {
	   //if(!_stricmp(tag->key,"_lib",4) && tag->key[4])
		  if(!_stricmp(tag->key,"_lib") && tag->key[4])
	   {
	    cache[cur].num=atoi(&tag->key[4]);
	    cache[cur].value=tag->value;
	    cur++;
	   }
	   tag=tag->next;
	  }
	  qsort(cache, libncount, sizeof(LIBNCACHE), ccomp);
	  for(cur=0;cur<libncount;cur++)
	  {
	   u32 ba[3];
	   wchar_t *tmpfn;

 	   if(cache[cur].num < 2) continue;

	   ba[0]=psxRegs.pc;
	   ba[1]=psxRegs.GPR.n.gp;
	   ba[2]=psxRegs.GPR.n.sp;

           /* Load file name "value" from the directory specified in
                the full path(directory + file name) "path"
             */
			wchar_t tempfn[_MAX_PATH];
			mbstowcs(tempfn, cache[cur].value, _MAX_PATH);

           tmpfn=GetFileWithBase(path,tempfn);
           if(!(tmpi=LoadPSF(tmpfn,level+1,0)))
           {
            //free(key);
            //free(value);
            //free(tmpfn);
            //fclose(fp);
            //return(0);
           }
           free(tmpfn);
	   FreeTags(tmpi->tags);
	   free(tmpi);

	   psxRegs.pc=ba[0];
       	   psxRegs.GPR.n.gp=ba[1];
	   psxRegs.GPR.n.sp=ba[2];
	  }
	  free(cache);

	 }	// if(libncount)

	}	// if(!type)

	return(psfi);
}

void sexy_freepsfinfo(PSFINFO *info)
{
 FreeTags(info->tags);
 free(info);
}

PSFINFO *sexy_getpsfinfo(const wchar_t *path)
{
	PSFINFO *ret;
        if(!(ret=LoadPSF(path,0,1))) return(0);
        if(ret->stop==~0) ret->fade=0;
        ret->length=ret->stop+ret->fade;
        return(ret);	
}

PSFINFO *sexy_load(const wchar_t *path) 
{
	PSFINFO *ret;

        psxInit();
        psxReset();

        SPUinit();
        SPUopen();

	if(!(ret=LoadPSF(path,0,0)))
	{
	 psxShutdown();
	 return(0);
	}

	if(ret->stop==~0) ret->fade=0; // Infinity+anything is still infinity...or is it?
//	SPUsetlength(ret->stop,ret->fade);
	ret->length=ret->stop+ret->fade;

        return(ret);
}

void sexy_execute(u32 numCycles)
{
 psxCpu->Execute(numCycles);
}

s8* GetPSXMainMemBuf()
{
	return psxM;
}
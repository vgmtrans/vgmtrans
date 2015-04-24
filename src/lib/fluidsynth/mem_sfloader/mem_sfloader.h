/* FluidSynth - A Software Synthesizer
 *
 * Copyright (C) 2003  Peter Hanappe and others.
 *
 * SoundFont loading code borrowed from Smurf SoundFont Editor by Josh Green
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */


#ifndef _FLUID_memsfont_H
#define _FLUID_memsfont_H

#include <stdint.h>

#include "fluidsynth.h"
#include "fluidsynth_priv.h"
#include "fluid_list.h"


/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/

/*** minimal gthread.h stuff to link to fluidsynth's glib threading functions ***/

#define g_thread_supported()     (1)

#ifndef GLIB_VAR
  #ifdef G_PLATFORM_WIN32
    #ifdef GLIB_STATIC_COMPILATION
      #define GLIB_VAR extern
    #else /* !GLIB_STATIC_COMPILATION */
      #ifdef GLIB_COMPILATION
        #ifdef DLL_EXPORT
          #define GLIB_VAR __declspec(dllexport)
        #else /* !DLL_EXPORT */
          #define GLIB_VAR extern
        #endif /* !DLL_EXPORT */
      #else /* !GLIB_COMPILATION */
        #define GLIB_VAR extern __declspec(dllimport)
      #endif /* !GLIB_COMPILATION */
    #endif /* !GLIB_STATIC_COMPILATION */
  #else /* !G_PLATFORM_WIN32 */
    #define GLIB_VAR extern
  #endif /* !G_PLATFORM_WIN32 */
#endif /* GLIB_VAR */

#define G_THREAD_UF(op, arglist)					\
    (*g_thread_functions_for_glib_use . op) arglist
#define G_THREAD_CF(op, fail, arg)					\
    (g_thread_supported () ? G_THREAD_UF (op, arg) : (fail))

#define g_mutex_lock(mutex)						\
    G_THREAD_CF (mutex_lock,     (void)0, (mutex))
#define g_mutex_trylock(mutex)						\
    G_THREAD_CF (mutex_trylock,  TRUE,    (mutex))
#define g_mutex_unlock(mutex)						\
    G_THREAD_CF (mutex_unlock,   (void)0, (mutex))
#define g_mutex_free(mutex)						\
    G_THREAD_CF (mutex_free,     (void)0, (mutex))

typedef union  _GMutex          GMutex;

union _GMutex
{
	/*< private >*/
	void* p;
	uint32_t i[2];
};

typedef struct _GThreadFunctions GThreadFunctions;
struct _GThreadFunctions
{
	GMutex*  (*mutex_new)     (void);
	void(*mutex_lock)         (GMutex               *mutex);
	int(*mutex_trylock)       (GMutex               *mutex);
	void(*mutex_unlock)       (GMutex               *mutex);
	void(*mutex_free)         (GMutex               *mutex);
};


GLIB_VAR GThreadFunctions       g_thread_functions_for_glib_use;

/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/

/*-----------------------------------sfont.h----------------------------*/

#define SF_SAMPMODES_LOOP	1
#define SF_SAMPMODES_UNROLL	2

#define SF_MIN_SAMPLERATE	400
#define SF_MAX_SAMPLERATE	50000

#define SF_MIN_SAMPLE_LENGTH	32


/* Sound Font structure defines */

typedef struct _SFVersion {
    /* version structure */
    unsigned short major;
    unsigned short minor;
}
        SFVersion;

typedef struct _SFMod {
    /* Modulator structure */
    unsigned short src;
    /* source modulator */
    unsigned short dest;
    /* destination generator */
    signed short amount;
    /* signed, degree of modulation */
    unsigned short amtsrc;
    /* second source controls amnt of first */
    unsigned short trans;        /* transform applied to source */
}
        SFMod;

typedef union _SFGenAmount {
    /* Generator amount structure */
    signed short sword;
    /* signed 16 bit value */
    unsigned short uword;
    /* unsigned 16 bit value */
    struct {
        unsigned char lo;
        /* low value for ranges */
        unsigned char hi;            /* high value for ranges */
    }
            range;
}
        SFGenAmount;

typedef struct _SFGen {
    /* Generator structure */
    unsigned short id;
    /* generator ID */
    SFGenAmount amount;        /* generator value */
}
        SFGen;

typedef struct _SFZone {
    /* Sample/instrument zone structure */
    fluid_list_t *instsamp;
    /* instrument/sample pointer for zone */
    fluid_list_t *gen;
    /* list of generators */
    fluid_list_t *mod;            /* list of modulators */
}
        SFZone;

typedef struct _SFSample {
    /* Sample structure */
    char name[21];
    /* Name of sample */
    unsigned char samfile;
    /* Loaded sfont/sample buffer = 0/1 */
    unsigned int start;
    /* Offset in sample area to start of sample */
    unsigned int end;
    /* Offset from start to end of sample,
				   this is the last point of the
				   sample, the SF spec has this as the
				   1st point after, corrected on
				   load/save */
    unsigned int loopstart;
    /* Offset from start to start of loop */
    unsigned int loopend;
    /* Offset from start to end of loop,
				   marks the first point after loop,
				   whose sample value is ideally
				   equivalent to loopstart */
    unsigned int samplerate;
    /* Sample rate recorded at */
    unsigned char origpitch;
    /* root midi key number */
    signed char pitchadj;
    /* pitch correction in cents */
    unsigned short sampletype;
    /* 1 mono,2 right,4 left,linked 8,0x8000=ROM */
    fluid_sample_t *fluid_sample;    /* Imported sample (fixed up in fluid_memsfont_load) */
}
        SFSample;

typedef struct _SFInst {
    /* Instrument structure */
    char name[21];
    /* Name of instrument */
    fluid_list_t *zone;            /* list of instrument zones */
}
        SFInst;

typedef struct _SFPreset {
    /* Preset structure */
    char name[21];
    /* preset name */
    unsigned short prenum;
    /* preset number */
    unsigned short bank;
    /* bank number */
    unsigned int libr;
    /* Not used (preserved) */
    unsigned int genre;
    /* Not used (preserved) */
    unsigned int morph;
    /* Not used (preserved) */
    fluid_list_t *zone;            /* list of preset zones */
}
        SFPreset;

/* NOTE: sffd is also used to determine if sound font is new (NULL) */
typedef struct _SFData {
    /* Sound font data structure */
    SFVersion version;
    /* sound font version */
    SFVersion romver;
    /* ROM version */
    unsigned int samplepos;
    /* position within sffd of the sample chunk */
    unsigned int samplesize;
    /* length within sffd of the sample chunk */
    char *fname;
    /* file name */
//    FILE *sffd;
    /* loaded sfont file descriptor */
    fluid_list_t *info;
    /* linked list of info strings (1st byte is ID) */
    fluid_list_t *preset;
    /* linked list of preset info */
    fluid_list_t *inst;
    /* linked list of instrument info */
    fluid_list_t *sample;        /* linked list of sample info */
}
        SFData;

/* sf file chunk IDs */
enum {
    UNKN_ID, RIFF_ID, LIST_ID, SFBK_ID,
    INFO_ID, SDTA_ID, PDTA_ID, /* info/sample/preset */

            IFIL_ID, ISNG_ID, INAM_ID, IROM_ID, /* info ids (1st byte of info strings) */
            IVER_ID, ICRD_ID, IENG_ID, IPRD_ID, /* more info ids */
            ICOP_ID, ICMT_ID, ISFT_ID, /* and yet more info ids */

            SNAM_ID, SMPL_ID, /* sample ids */
            PHDR_ID, PBAG_ID, PMOD_ID, PGEN_ID, /* preset ids */
            IHDR_ID, IBAG_ID, IMOD_ID, IGEN_ID, /* instrument ids */
            SHDR_ID            /* sample info */
};

/* generator types */
typedef enum {
    Gen_StartAddrOfs, Gen_EndAddrOfs, Gen_StartLoopAddrOfs,
    Gen_EndLoopAddrOfs, Gen_StartAddrCoarseOfs, Gen_ModLFO2Pitch,
    Gen_VibLFO2Pitch, Gen_ModEnv2Pitch, Gen_FilterFc, Gen_FilterQ,
    Gen_ModLFO2FilterFc, Gen_ModEnv2FilterFc, Gen_EndAddrCoarseOfs,
    Gen_ModLFO2Vol, Gen_Unused1, Gen_ChorusSend, Gen_ReverbSend, Gen_Pan,
    Gen_Unused2, Gen_Unused3, Gen_Unused4,
    Gen_ModLFODelay, Gen_ModLFOFreq, Gen_VibLFODelay, Gen_VibLFOFreq,
    Gen_ModEnvDelay, Gen_ModEnvAttack, Gen_ModEnvHold, Gen_ModEnvDecay,
    Gen_ModEnvSustain, Gen_ModEnvRelease, Gen_Key2ModEnvHold,
    Gen_Key2ModEnvDecay, Gen_VolEnvDelay, Gen_VolEnvAttack,
    Gen_VolEnvHold, Gen_VolEnvDecay, Gen_VolEnvSustain, Gen_VolEnvRelease,
    Gen_Key2VolEnvHold, Gen_Key2VolEnvDecay, Gen_Instrument,
    Gen_Reserved1, Gen_KeyRange, Gen_VelRange,
    Gen_StartLoopAddrCoarseOfs, Gen_Keynum, Gen_Velocity,
    Gen_Attenuation, Gen_Reserved2, Gen_EndLoopAddrCoarseOfs,
    Gen_CoarseTune, Gen_FineTune, Gen_SampleId, Gen_SampleModes,
    Gen_Reserved3, Gen_ScaleTune, Gen_ExclusiveClass, Gen_OverrideRootKey,
    Gen_Dummy
}
        Gen_Type;

#define Gen_MaxValid    Gen_Dummy - 1    /* maximum valid generator */
#define Gen_Count    Gen_Dummy    /* count of generators */
#define GenArrSize sizeof(SFGenAmount)*Gen_Count    /* gen array size */

/* generator unit type */
typedef enum {
    None, /* No unit type */
            Unit_Smpls, /* in samples */
            Unit_32kSmpls, /* in 32k samples */
            Unit_Cent, /* in cents (1/100th of a semitone) */
            Unit_HzCent, /* in Hz Cents */
            Unit_TCent, /* in Time Cents */
            Unit_cB, /* in centibels (1/100th of a decibel) */
            Unit_Percent, /* in percentage */
            Unit_Semitone, /* in semitones */
            Unit_Range            /* a range of values */
}
        Gen_Unit;

/* global data */

extern unsigned short mem_badgen[];
/* list of bad generators */
extern unsigned short mem_badpgen[];    /* list of bad preset generators */

/* functions */
void mem_sfont_init_chunks(void);

void mem_sfont_close(SFData *sf);
void mem_sfont_free_zone(SFZone *zone);
int mem_sfont_preset_compare_func(void *a, void *b);

void mem_sfont_zone_delete(SFData *sf, fluid_list_t **zlist, SFZone *zone);

fluid_list_t *mem_gen_inlist(int gen, fluid_list_t *genlist);
int mem_gen_valid(int gen);
int mem_gen_validp(int gen);


/*-----------------------------------sffile.h----------------------------*/
/*
   File structures and routines (used to be in sffile.h)
*/

#define CHNKIDSTR(id)           &idlist[(id - 1) * 4]

/* sfont file chunk sizes */
#define SFPHDRSIZE    38
#define SFBAGSIZE    4
#define SFMODSIZE    10
#define SFGENSIZE    4
#define SFIHDRSIZE    22
#define SFSHDRSIZE    46

/* sfont file data structures */
typedef struct _SFChunk {
    /* RIFF file chunk structure */
    unsigned int id;
    /* chunk id */
    unsigned int size;            /* size of the following chunk */
}
        SFChunk;

typedef struct _SFPhdr {
    unsigned char name[20];
    /* preset name */
    unsigned short preset;
    /* preset number */
    unsigned short bank;
    /* bank number */
    unsigned short pbagndx;
    /* index into preset bag */
    unsigned int library;
    /* just for preserving them */
    unsigned int genre;
    /* Not used */
    unsigned int morphology;        /* Not used */
}
        SFPhdr;

typedef struct _SFBag {
    unsigned short genndx;
    /* index into generator list */
    unsigned short modndx;        /* index into modulator list */
}
        SFBag;

typedef struct _SFIhdr {
    char name[20];
    /* Name of instrument */
    unsigned short ibagndx;        /* Instrument bag index */
}
        SFIhdr;

typedef struct _SFShdr {
    /* Sample header loading struct */
    char name[20];
    /* Sample name */
    unsigned int start;
    /* Offset to start of sample */
    unsigned int end;
    /* Offset to end of sample */
    unsigned int loopstart;
    /* Offset to start of loop */
    unsigned int loopend;
    /* Offset to end of loop */
    unsigned int samplerate;
    /* Sample rate recorded at */
    unsigned char origpitch;
    /* root midi key number */
    signed char pitchadj;
    /* pitch correction in cents */
    unsigned short samplelink;
    /* Not used */
    unsigned short sampletype;        /* 1 mono,2 right,4 left,linked 8,0x8000=ROM */
}
        SFShdr;

/* data */
extern char idlist[];

/* functions */
SFData *sfload_mem(const void* data);



/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/

/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02110-1301, USA.
 */

//#include <glib.h>


/*-----------------------------------util.h----------------------------*/
/*
  Utility functions (formerly in util.h)
 */
#define FAIL    0
#define OK    1

enum {
    ErrWarn, ErrFatal, ErrStatus, ErrCorr, ErrEof, ErrMem, Errno,
    ErrRead, ErrWrite
};

#define ErrMax        ErrWrite
#define ErrnoStart    Errno
#define ErrnoEnd    ErrWrite

int mem_gerr(int ev, char *fmt, ...);
int mem_safe_fread (void *buf, int count, uint8_t ** data);
//int mem_safe_fwrite(void *buf, int count, FILE *fd);
int mem_safe_fseek (uint8_t ** data, long ofs, int whence);


/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/



/***************************************************************
*
*       FORWARD DECLARATIONS
*/
typedef struct _memsfont_t memsfont_t;
typedef struct _mempreset_t mempreset_t;
typedef struct _preset_zone_t preset_zone_t;
typedef struct _inst_t inst_t;
typedef struct _inst_zone_t inst_zone_t;

/*

  Public interface

 */
void bloggityBlog();

fluid_sfloader_t *new_memsfloader(fluid_settings_t *settings);
int delete_memsfloader(fluid_sfloader_t *loader);
fluid_sfont_t *memsfloader_load(fluid_sfloader_t *loader, const char *filename);


int memsfont_sfont_delete(fluid_sfont_t *sfont);
char *memsfont_sfont_get_name(fluid_sfont_t *sfont);
fluid_preset_t *memsfont_sfont_get_preset(fluid_sfont_t *sfont, unsigned int bank, unsigned int prenum);
void memsfont_sfont_iteration_start(fluid_sfont_t *sfont);
int memsfont_sfont_iteration_next(fluid_sfont_t *sfont, fluid_preset_t *preset);


int mempreset_preset_delete(fluid_preset_t *preset);
char *mempreset_preset_get_name(fluid_preset_t *preset);
int mempreset_preset_get_banknum(fluid_preset_t *preset);
int mempreset_preset_get_num(fluid_preset_t *preset);
int mempreset_preset_noteon(fluid_preset_t *preset, fluid_synth_t *synth, int chan, int key, int vel);


/*
 * fluid_memsfont_t
 */
struct _memsfont_t {
    char *filename;
    /* the filename of this soundfont */
    unsigned int samplepos;
    /* the position in the file at which the sample data starts */
    unsigned int samplesize;
    /* the size of the sample data */
    short *sampledata;
    /* the sample data, loaded in ram */
    fluid_list_t *sample;
    /* the samples in this soundfont */
    mempreset_t *preset;
    /* the presets of this soundfont */
    int mlock;
    /* Should we try memlock (avoid swapping)? */

    fluid_preset_t iter_preset;
    /* preset interface used in the iteration */
    mempreset_t *iter_cur;
    /* the current preset in the iteration */

    fluid_preset_t **preset_stack;
    /* List of presets that are available to use */
    int preset_stack_capacity;
    /* Length of preset_stack array */
    int preset_stack_size;         /* Current number of items in the stack */
};


memsfont_t *new_memsfont(fluid_settings_t *settings);
int delete_memsfont(memsfont_t *sfont);
int memsfont_load(memsfont_t *sfont, const void*file);
char *memsfont_get_name(memsfont_t *sfont);
mempreset_t *memsfont_get_preset(memsfont_t *sfont, unsigned int bank, unsigned int prenum);
void memsfont_iteration_start(memsfont_t *sfont);
int memsfont_iteration_next(memsfont_t *sfont, fluid_preset_t *preset);
int memsfont_load_sampledata(memsfont_t *sfont, const void * data);
int memsfont_add_sample(memsfont_t *sfont, fluid_sample_t *sample);
int memsfont_add_preset(memsfont_t *sfont, mempreset_t *preset);


/*
 * fluid_preset_t
 */
struct _mempreset_t {
    mempreset_t *next;
    memsfont_t *sfont;
    /* the soundfont this preset belongs to */
    char name[21];
    /* the name of the preset */
    unsigned int bank;
    /* the bank number */
    unsigned int num;
    /* the preset number */
    preset_zone_t *global_zone;
    /* the global zone of the preset */
    preset_zone_t *zone;               /* the chained list of preset zones */
};

mempreset_t *new_mempreset(memsfont_t *sfont);
int delete_mempreset(mempreset_t *preset);
mempreset_t *mempreset_next(mempreset_t *preset);
int mempreset_import_sfont(mempreset_t *preset, SFPreset *sfpreset, memsfont_t *sfont);
int mempreset_set_global_zone(mempreset_t *preset, preset_zone_t *zone);
int mempreset_add_zone(mempreset_t *preset, preset_zone_t *zone);
preset_zone_t *mempreset_get_zone(mempreset_t *preset);
preset_zone_t *mempreset_get_global_zone(mempreset_t *preset);
int mempreset_get_banknum(mempreset_t *preset);
int mempreset_get_num(mempreset_t *preset);
char *mempreset_get_name(mempreset_t *preset);
int mempreset_noteon(mempreset_t *preset, fluid_synth_t *synth, int chan, int key, int vel);

/*
 * preset_zone
 */
struct _preset_zone_t {
    preset_zone_t *next;
    char *name;
    inst_t *inst;
    int keylo;
    int keyhi;
    int vello;
    int velhi;
    fluid_gen_t gen[GEN_LAST];
    fluid_mod_t *mod; /* List of modulators */
};

preset_zone_t *new_preset_zone(char *name);
int delete_preset_zone(preset_zone_t *zone);
preset_zone_t *preset_zone_next(preset_zone_t *preset);
int preset_zone_import_sfont(preset_zone_t *zone, SFZone *sfzone, memsfont_t *sfont);
int preset_zone_inside_range(preset_zone_t *zone, int key, int vel);
inst_t *preset_zone_get_inst(preset_zone_t *zone);

/*
 * inst_t
 */
struct _inst_t {
    char name[21];
    inst_zone_t *global_zone;
    inst_zone_t *zone;
};

inst_t *new_inst(void);
int delete_inst(inst_t *inst);
int inst_import_sfont(inst_t *inst, SFInst *sfinst, memsfont_t *sfont);
int inst_set_global_zone(inst_t *inst, inst_zone_t *zone);
int inst_add_zone(inst_t *inst, inst_zone_t *zone);
inst_zone_t *inst_get_zone(inst_t *inst);
inst_zone_t *inst_get_global_zone(inst_t *inst);

/*
 * inst_zone_t
 */
struct _inst_zone_t {
    inst_zone_t *next;
    char *name;
    fluid_sample_t *sample;
    int keylo;
    int keyhi;
    int vello;
    int velhi;
    fluid_gen_t gen[GEN_LAST];
    fluid_mod_t *mod; /* List of modulators */
};

inst_zone_t *new_inst_zone(char *name);
int delete_inst_zone(inst_zone_t *zone);
inst_zone_t *inst_zone_next(inst_zone_t *zone);
int inst_zone_import_sfont(inst_zone_t *zone, SFZone *sfzone, memsfont_t *sfont);
int inst_zone_inside_range(inst_zone_t *zone, int key, int vel);
fluid_sample_t *inst_zone_get_sample(inst_zone_t *zone);


fluid_sample_t *new_sample(void);
int delete_sample(fluid_sample_t *sample);
int sample_import_sfont(fluid_sample_t *sample, SFSample *sfsample, memsfont_t *sfont);
int sample_in_rom(fluid_sample_t *sample);


#endif  /* _FLUID_SFONT_H */

/* FluidSynth - A Software Synthesizer
 *
 * Copyright (C) 2003  Peter Hanappe and others.
 *
 * SoundFont file loading code borrowed from Smurf SoundFont Editor
 * Copyright (C) 1999-2001 Josh Green
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


#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include "mem_sfloader.h"

//#include "sfont.h"
/* Todo: Get rid of that 'include' */
//#include "fluid_sys.h"


//***************************************************************
// Some definitions and macros taken out of fluid_sys and glib

#define IS_BIG_ENDIAN (*(uint16_t *)"\0\xff" < 0x100)
#define FLUID_IS_BIG_ENDIAN IS_BIG_ENDIAN

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef	TRUE
#define	TRUE	(!FALSE)
#endif

typedef int16_t gint16;
typedef int32_t gint32;
typedef uint32_t guint32;

#if !(defined (G_STMT_START) && defined (G_STMT_END))
#define G_STMT_START  do
#define G_STMT_END    while (0)
#endif

#define GINT16_TO_LE(val)	((gint16) (val))
#define GINT32_TO_LE(val)	((gint32) (val))
#define GUINT32_TO_LE(val)	((guint32) (val))

#define GINT16_FROM_LE(val)	(GINT16_TO_LE (val))
#define GINT32_FROM_LE(val)	(GINT32_TO_LE (val))
#define GUINT32_FROM_LE(val)	(GUINT32_TO_LE (val))

//#define GPOINTER_TO_INT(p)	((gint)  (glong) (p))
//#define GINT_TO_POINTER(i)	((gpointer) (glong) (i))

/***************************************************************
*
*                           SFONT LOADER
*/

fluid_sfloader_t* new_memsfloader(fluid_settings_t* settings)
{
    fluid_sfloader_t* loader;

    loader = FLUID_NEW(fluid_sfloader_t);
    if (loader == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }

    loader->data = settings;
    loader->free = delete_memsfloader;
    loader->load = memsfloader_load;

    return loader;
}

int delete_memsfloader(fluid_sfloader_t* loader)
{
    if (loader) {
        FLUID_FREE(loader);
    }
    return OK;
}

fluid_sfont_t* memsfloader_load(fluid_sfloader_t* loader, const char* filename)
{
    memsfont_t* memsfont;
    fluid_sfont_t* sfont;

    printf("CALLED MEMSFLOADER_LOAD");

    const void* data = (const void*)filename;

    memsfont = new_memsfont(loader->data);

    if (memsfont == NULL) {
        return NULL;
    }

    if (memsfont_load(memsfont, data) == FLUID_FAILED) {
        delete_memsfont(memsfont);
        return NULL;
    }

    sfont = FLUID_NEW(fluid_sfont_t);
    if (sfont == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }

    sfont->data = memsfont;
    sfont->free = memsfont_sfont_delete;
    sfont->get_name = memsfont_sfont_get_name;
    sfont->get_preset = memsfont_sfont_get_preset;
    sfont->iteration_start = memsfont_sfont_iteration_start;
    sfont->iteration_next = memsfont_sfont_iteration_next;

    return sfont;
}



/***************************************************************
*
*                           PUBLIC INTERFACE
*/

int memsfont_sfont_delete(fluid_sfont_t* sfont)
{
    if (delete_memsfont(sfont->data) != 0) {
        return -1;
    }
    FLUID_FREE(sfont);
    return 0;
}

char* memsfont_sfont_get_name(fluid_sfont_t* sfont)
{
    return memsfont_get_name((memsfont_t*) sfont->data);
}

fluid_sample_t* memsfont_get_sample(memsfont_t* sfont, char *s)
{
    /* This function is here just to avoid an ABI/SONAME bump, see ticket #98. Should never be used. */
    return NULL;
}

fluid_preset_t*
memsfont_sfont_get_preset(fluid_sfont_t* sfont, unsigned int bank, unsigned int prenum)
{
    fluid_preset_t* preset = NULL;
    mempreset_t* mempreset;
    memsfont_t* memsfont = sfont->data;

    mempreset = memsfont_get_preset(memsfont, bank, prenum);

    if (mempreset == NULL) {
        return NULL;
    }

    if (memsfont->preset_stack_size > 0) {
        memsfont->preset_stack_size--;
        preset = memsfont->preset_stack[memsfont->preset_stack_size];
    }
    if (!preset)
        preset = FLUID_NEW(fluid_preset_t);
    if (!preset) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }

    preset->sfont = sfont;
    preset->data = mempreset;
    preset->free = mempreset_preset_delete;
    preset->get_name = mempreset_preset_get_name;
    preset->get_banknum = mempreset_preset_get_banknum;
    preset->get_num = mempreset_preset_get_num;
    preset->noteon = mempreset_preset_noteon;
    preset->notify = NULL;

    return preset;
}

void memsfont_sfont_iteration_start(fluid_sfont_t* sfont)
{
    memsfont_iteration_start((memsfont_t*) sfont->data);
}

int memsfont_sfont_iteration_next(fluid_sfont_t* sfont, fluid_preset_t* preset)
{
    preset->free = mempreset_preset_delete;
    preset->get_name = mempreset_preset_get_name;
    preset->get_banknum = mempreset_preset_get_banknum;
    preset->get_num = mempreset_preset_get_num;
    preset->noteon = mempreset_preset_noteon;
    preset->notify = NULL;

    return memsfont_iteration_next((memsfont_t*) sfont->data, preset);
}

int mempreset_preset_delete(fluid_preset_t* preset)
{
    mempreset_t* mempreset = preset ? preset->data : NULL;
    memsfont_t* sfont = mempreset ? mempreset->sfont : NULL;

    if (sfont && sfont->preset_stack_size < sfont->preset_stack_capacity) {
        sfont->preset_stack[sfont->preset_stack_size] = preset;
        sfont->preset_stack_size++;
    }
    else
        FLUID_FREE(preset);

    return 0;
}

char* mempreset_preset_get_name(fluid_preset_t* preset)
{
    return mempreset_get_name((mempreset_t*) preset->data);
}

int mempreset_preset_get_banknum(fluid_preset_t* preset)
{
    return mempreset_get_banknum((mempreset_t*) preset->data);
}

int mempreset_preset_get_num(fluid_preset_t* preset)
{
    return mempreset_get_num((mempreset_t*) preset->data);
}

int mempreset_preset_noteon(fluid_preset_t* preset, fluid_synth_t* synth,
        int chan, int key, int vel)
{
    return mempreset_noteon((mempreset_t*) preset->data, synth, chan, key, vel);
}




/***************************************************************
*
*                    CACHED SAMPLEDATA LOADER
*/

typedef struct _cached_sampledata_t {
    struct _cached_sampledata_t *next;

    char* filename;
    time_t modification_time;
    int num_references;
    int mlock;

    const short* sampledata;
    unsigned int samplesize;
} cached_sampledata_t;

static cached_sampledata_t* all_cached_sampledata = NULL;
//static fluid_mutex_t cached_sampledata_mutex = FLUID_MUTEX_INIT;

static int get_file_modification_time(char *filename, time_t *modification_time)
{
#if defined(WIN32) || defined(__OS2__)
  *modification_time = 0;
  return OK;
#else
    struct stat buf;

    if (stat(filename, &buf) == -1) {
        return FLUID_FAILED;
    }

    *modification_time = buf.st_mtime;
    return OK;
#endif
}

static int cached_sampledata_load(const void ** data, unsigned int samplepos,
        unsigned int samplesize, short **sampledata, int try_mlock)
{
//    fluid_file fd = NULL;
    short *loaded_sampledata = NULL;
    cached_sampledata_t* cached_sampledata = NULL;
    time_t modification_time;

//    fluid_mutex_lock(cached_sampledata_mutex);

//    if (get_file_modification_time(filename, &modification_time) == FLUID_FAILED) {
//        FLUID_LOG(FLUID_WARN, "Unable to read modificaton time of soundfont file.");
        modification_time = 0;
//    }

//    for (cached_sampledata = all_cached_sampledata; cached_sampledata; cached_sampledata = cached_sampledata->next) {
//        if (strcmp(filename, cached_sampledata->filename))
//            continue;
//        if (cached_sampledata->modification_time != modification_time)
//            continue;
//        if (cached_sampledata->samplesize != samplesize) {
//            FLUID_LOG(FLUID_ERR, "Cached size of soundfont doesn't match actual size of soundfont (cached: %u. actual: %u)",
//                    cached_sampledata->samplesize, samplesize);
//            continue;
//        }
//
//        if (try_mlock && !cached_sampledata->mlock) {
//            if (fluid_mlock(cached_sampledata->sampledata, samplesize) != 0)
//                FLUID_LOG(FLUID_WARN, "Failed to pin the sample data to RAM; swapping is possible.");
//            else
//                cached_sampledata->mlock = try_mlock;
//        }
//
//        cached_sampledata->num_references++;
//        loaded_sampledata = (short*) cached_sampledata->sampledata;
//        goto success_exit;
//    }

//    fd = FLUID_FOPEN(filename, "rb");
//    if (fd == NULL) {
//        FLUID_LOG(FLUID_ERR, "Can't open soundfont file");
//        goto error_exit;
//    }
//    if (FLUID_FSEEK(fd, samplepos, SEEK_SET) == -1) {
//        perror("error");
//        FLUID_LOG(FLUID_ERR, "Failed to seek position in data file");
//        goto error_exit;
//    }
    mem_safe_fseek(data, samplepos, SEEK_CUR);


    loaded_sampledata = (short*) FLUID_MALLOC(samplesize);
    if (loaded_sampledata == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        goto error_exit;
    }
//    if (FLUID_FREAD(loaded_sampledata, 1, samplesize, fd) < samplesize) {
//        FLUID_LOG(FLUID_ERR, "Failed to read sample data");
//        goto error_exit;
//    }
    mem_safe_fread(loaded_sampledata, samplesize, data);

//    FLUID_FCLOSE(fd);
//    fd = NULL;


    cached_sampledata = (cached_sampledata_t*) FLUID_MALLOC(sizeof(cached_sampledata_t));
    if (cached_sampledata == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory.");
        goto error_exit;
    }

    /* Lock the memory to disable paging. It's okay if this fails. It
       probably means that the user doesn't have to required permission.  */
    cached_sampledata->mlock = 0;
    if (try_mlock) {
//        if (fluid_mlock(loaded_sampledata, samplesize) != 0)
//            FLUID_LOG(FLUID_WARN, "Failed to pin the sample data to RAM; swapping is possible.");
//        else
            cached_sampledata->mlock = try_mlock;
    }

    /* If this machine is big endian, the sample have to byte swapped  */
    if (FLUID_IS_BIG_ENDIAN) {
        unsigned char* cbuf;
        unsigned char hi, lo;
        unsigned int i, j;
        short s;
        cbuf = (unsigned char*)loaded_sampledata;
        for (i = 0, j = 0; j < samplesize; i++) {
            lo = cbuf[j++];
            hi = cbuf[j++];
            s = (hi << 8) | lo;
            loaded_sampledata[i] = s;
        }
    }

//    cached_sampledata->filename = (char*) FLUID_MALLOC(strlen(filename) + 1);
//    if (cached_sampledata->filename == NULL) {
//        FLUID_LOG(FLUID_ERR, "Out of memory.");
//        goto error_exit;
//    }

//    sprintf(cached_sampledata->filename, "%s", filename);
    cached_sampledata->modification_time = modification_time;
    cached_sampledata->num_references = 1;
    cached_sampledata->sampledata = loaded_sampledata;
    cached_sampledata->samplesize = samplesize;

    cached_sampledata->next = all_cached_sampledata;
    all_cached_sampledata = cached_sampledata;


    success_exit:
//    fluid_mutex_unlock(cached_sampledata_mutex);
    *sampledata = loaded_sampledata;
    return OK;

    error_exit:
//    if (fd != NULL) {
//        FLUID_FCLOSE(fd);
//    }
    if (loaded_sampledata != NULL) {
        FLUID_FREE(loaded_sampledata);
    }

    if (cached_sampledata != NULL) {
        if (cached_sampledata->filename != NULL) {
            FLUID_FREE(cached_sampledata->filename);
        }
        FLUID_FREE(cached_sampledata);
    }

//    fluid_mutex_unlock(cached_sampledata_mutex);
    *sampledata = NULL;
    return FLUID_FAILED;
}

static int cached_sampledata_unload(const short *sampledata)
{
    cached_sampledata_t* prev = NULL;
    cached_sampledata_t* cached_sampledata;

//    fluid_mutex_lock(cached_sampledata_mutex);
    cached_sampledata = all_cached_sampledata;

    while (cached_sampledata != NULL) {
        if (sampledata == cached_sampledata->sampledata) {

            cached_sampledata->num_references--;

            if (cached_sampledata->num_references == 0) {
//                if (cached_sampledata->mlock)
//                    fluid_munlock(cached_sampledata->sampledata, cached_sampledata->samplesize);
                FLUID_FREE((short*) cached_sampledata->sampledata);
                FLUID_FREE(cached_sampledata->filename);

                if (prev != NULL) {
                    prev->next = cached_sampledata->next;
                } else {
                    all_cached_sampledata = cached_sampledata->next;
                }

                FLUID_FREE(cached_sampledata);
            }

            goto success_exit;
        }

        prev = cached_sampledata;
        cached_sampledata = cached_sampledata->next;
    }

    FLUID_LOG(FLUID_ERR, "Trying to free sampledata not found in cache.");
    goto error_exit;

    success_exit:
//    fluid_mutex_unlock(cached_sampledata_mutex);
    return OK;

    error_exit:
//    fluid_mutex_unlock(cached_sampledata_mutex);
    return FLUID_FAILED;
}




/***************************************************************
*
*                           SFONT
*/

/*
 * new_memsfont
 */
memsfont_t* new_memsfont(fluid_settings_t* settings)
{
    memsfont_t* sfont;
    int i;

    sfont = FLUID_NEW(memsfont_t);
    if (sfont == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }

//    sfont->filename = NULL;
    sfont->samplepos = 0;
    sfont->samplesize = 0;
    sfont->sample = NULL;
    sfont->sampledata = NULL;
    sfont->preset = NULL;
    fluid_settings_getint(settings, "synth.lock-memory", &sfont->mlock);

    /* Initialise preset cache, so we don't have to call malloc on program changes.
       Usually, we have at most one preset per channel plus one temporarily used,
       so optimise for that case. */
    fluid_settings_getint(settings, "synth.midi-channels", &sfont->preset_stack_capacity);
    sfont->preset_stack_capacity++;
    sfont->preset_stack_size = 0;
    sfont->preset_stack = FLUID_ARRAY(fluid_preset_t*, sfont->preset_stack_capacity);
    if (!sfont->preset_stack) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        FLUID_FREE(sfont);
        return NULL;
    }

    for (i = 0; i < sfont->preset_stack_capacity; i++) {
        sfont->preset_stack[i] = FLUID_NEW(fluid_preset_t);
        if (!sfont->preset_stack[i]) {
            FLUID_LOG(FLUID_ERR, "Out of memory");
            delete_memsfont(sfont);
            return NULL;
        }
        sfont->preset_stack_size++;
    }

    return sfont;
}

/*
 * delete_memsfont
 */
int delete_memsfont(memsfont_t* sfont)
{
    fluid_list_t *list;
    mempreset_t* preset;
    fluid_sample_t* sample;

    /* Check that no samples are currently used */
    for (list = sfont->sample; list; list = fluid_list_next(list)) {
        sample = (fluid_sample_t*) fluid_list_get(list);
        if (fluid_sample_refcount(sample) != 0) {
            return -1;
        }
    }

//    if (sfont->filename != NULL) {
//        FLUID_FREE(sfont->filename);
//    }

    for (list = sfont->sample; list; list = fluid_list_next(list)) {
        delete_sample((fluid_sample_t*) fluid_list_get(list));
    }

    if (sfont->sample) {
        delete_fluid_list(sfont->sample);
    }

    if (sfont->sampledata != NULL) {
        cached_sampledata_unload(sfont->sampledata);
    }

    while (sfont->preset_stack_size > 0)
        FLUID_FREE(sfont->preset_stack[--sfont->preset_stack_size]);
    FLUID_FREE(sfont->preset_stack);

    preset = sfont->preset;
    while (preset != NULL) {
        sfont->preset = preset->next;
        delete_mempreset(preset);
        preset = sfont->preset;
    }

    FLUID_FREE(sfont);
    return OK;
}

/*
 * memsfont_get_name
 */
char* memsfont_get_name(memsfont_t* sfont)
{
    return sfont->filename;
}


/*
 * memsfont_load
 */
int memsfont_load(memsfont_t* sfont, const void* data)
{
    SFData* sfdata;
    fluid_list_t *p;
    SFPreset* sfpreset;
    SFSample* sfsample;
    fluid_sample_t* sample;
    mempreset_t* preset;

    sfont->filename = "SF2 Filename";//FLUID_MALLOC(1 + FLUID_STRLEN(file));
//    if (sfont->filename == NULL) {
//        FLUID_LOG(FLUID_ERR, "Out of memory");
//        return FLUID_FAILED;
//    }
//    FLUID_STRCPY(sfont->filename, file);

    /* The actual loading is done in the sfont and sffile files */
    sfdata = sfload_mem(data);
    if (sfdata == NULL) {
        FLUID_LOG(FLUID_ERR, "Couldn't load soundfont file");
        return FLUID_FAILED;
    }

    /* Keep track of the position and size of the sample data because
       it's loaded separately (and might be unoaded/reloaded in future) */
    sfont->samplepos = sfdata->samplepos;
    sfont->samplesize = sfdata->samplesize;

    printf("LOADING SAMPLE DATA... RUH ROH");
    /* load sample data in one block */
    if (memsfont_load_sampledata(sfont, data) != OK)
        goto err_exit;

    /* Create all the sample headers */
    p = sfdata->sample;
    while (p != NULL) {
        sfsample = (SFSample *) p->data;

        sample = new_sample();
        if (sample == NULL) goto err_exit;

        if (sample_import_sfont(sample, sfsample, sfont) != OK)
            goto err_exit;

        /* Store reference to FluidSynth sample in SFSample for later IZone fixups */
        sfsample->fluid_sample = sample;

        memsfont_add_sample(sfont, sample);
        fluid_voice_optimize_sample(sample);
        p = fluid_list_next(p);
    }

    /* Load all the presets */
    p = sfdata->preset;
    while (p != NULL) {
        sfpreset = (SFPreset *) p->data;
        preset = new_mempreset(sfont);
        if (preset == NULL) goto err_exit;

        if (mempreset_import_sfont(preset, sfpreset, sfont) != OK)
            goto err_exit;

        memsfont_add_preset(sfont, preset);
        p = fluid_list_next(p);
    }
    mem_sfont_close (sfdata);

    return OK;

    err_exit:
    mem_sfont_close (sfdata);
    return FLUID_FAILED;
}

/* memsfont_add_sample
 *
 * Add a sample to the SoundFont
 */
int memsfont_add_sample(memsfont_t* sfont, fluid_sample_t* sample)
{
    sfont->sample = fluid_list_append(sfont->sample, sample);
    return OK;
}

/* memsfont_add_preset
 *
 * Add a preset to the SoundFont
 */
int memsfont_add_preset(memsfont_t* sfont, mempreset_t* preset)
{
    mempreset_t *cur, *prev;
    if (sfont->preset == NULL) {
        preset->next = NULL;
        sfont->preset = preset;
    } else {
        /* sort them as we go along. very basic sorting trick. */
        cur = sfont->preset;
        prev = NULL;
        while (cur != NULL) {
            if ((preset->bank < cur->bank)
                    || ((preset->bank == cur->bank) && (preset->num < cur->num))) {
                if (prev == NULL) {
                    preset->next = cur;
                    sfont->preset = preset;
                } else {
                    preset->next = cur;
                    prev->next = preset;
                }
                return OK;
            }
            prev = cur;
            cur = cur->next;
        }
        preset->next = NULL;
        prev->next = preset;
    }
    return OK;
}

/*
 * memsfont_load_sampledata
 */
int
memsfont_load_sampledata(memsfont_t* sfont, const void * data)
{
    return cached_sampledata_load(&data, sfont->samplepos,
            sfont->samplesize, &sfont->sampledata, sfont->mlock);
}

/*
 * memsfont_get_preset
 */
mempreset_t* memsfont_get_preset(memsfont_t* sfont, unsigned int bank, unsigned int num)
{
    mempreset_t* preset = sfont->preset;
    while (preset != NULL) {
        if ((preset->bank == bank) && ((preset->num == num))) {
            return preset;
        }
        preset = preset->next;
    }
    return NULL;
}

/*
 * memsfont_iteration_start
 */
void memsfont_iteration_start(memsfont_t* sfont)
{
    sfont->iter_cur = sfont->preset;
}

/*
 * memsfont_iteration_next
 */
int memsfont_iteration_next(memsfont_t* sfont, fluid_preset_t* preset)
{
    if (sfont->iter_cur == NULL) {
        return 0;
    }

    preset->data = (void*) sfont->iter_cur;
    sfont->iter_cur = mempreset_next(sfont->iter_cur);
    return 1;
}

/***************************************************************
*
*                           PRESET
*/

/*
 * new_mempreset
 */
mempreset_t*
new_mempreset(memsfont_t* sfont)
{
    mempreset_t* preset = FLUID_NEW(mempreset_t);
    if (preset == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    preset->next = NULL;
    preset->sfont = sfont;
    preset->name[0] = 0;
    preset->bank = 0;
    preset->num = 0;
    preset->global_zone = NULL;
    preset->zone = NULL;
    return preset;
}

/*
 * delete_mempreset
 */
int
delete_mempreset(mempreset_t* preset)
{
    int err = OK;
    preset_zone_t* zone;
    if (preset->global_zone != NULL) {
        if (delete_preset_zone(preset->global_zone) != OK) {
            err = FLUID_FAILED;
        }
        preset->global_zone = NULL;
    }
    zone = preset->zone;
    while (zone != NULL) {
        preset->zone = zone->next;
        if (delete_preset_zone(zone) != OK) {
            err = FLUID_FAILED;
        }
        zone = preset->zone;
    }
    FLUID_FREE(preset);
    return err;
}

int
mempreset_get_banknum(mempreset_t* preset)
{
    return preset->bank;
}

int
mempreset_get_num(mempreset_t* preset)
{
    return preset->num;
}

char*
mempreset_get_name(mempreset_t* preset)
{
    return preset->name;
}

/*
 * mempreset_next
 */
mempreset_t*
mempreset_next(mempreset_t* preset)
{
    return preset->next;
}


/*
 * mempreset_noteon
 */
int
mempreset_noteon(mempreset_t* preset, fluid_synth_t* synth, int chan, int key, int vel)
{
    preset_zone_t *preset_zone, *global_preset_zone;
    inst_t* inst;
    inst_zone_t *inst_zone, *global_inst_zone;
    fluid_sample_t* sample;
    fluid_voice_t* voice;
    fluid_mod_t * mod;
    fluid_mod_t * FLUID_MOD_list[FLUID_NUM_MOD]; /* list for 'sorting' preset modulators */
    int FLUID_MOD_list_count;
    int i;

    global_preset_zone = mempreset_get_global_zone(preset);

    /* run thru all the zones of this preset */
    preset_zone = mempreset_get_zone(preset);
    while (preset_zone != NULL) {

        /* check if the note falls into the key and velocity range of this
           preset */
        if (preset_zone_inside_range(preset_zone, key, vel)) {

            inst = preset_zone_get_inst(preset_zone);
            global_inst_zone = inst_get_global_zone(inst);

            /* run thru all the zones of this instrument */
            inst_zone = inst_get_zone(inst);
            while (inst_zone != NULL) {

                /* make sure this instrument zone has a valid sample */
                sample = inst_zone_get_sample(inst_zone);
                if ((sample == NULL) || sample_in_rom(sample)) {
                    inst_zone = inst_zone_next(inst_zone);
                    continue;
                }

                /* check if the note falls into the key and velocity range of this
                   instrument */

                if (inst_zone_inside_range(inst_zone, key, vel) && (sample != NULL)) {

                    /* this is a good zone. allocate a new synthesis process and
                           initialize it */

                    voice = fluid_synth_alloc_voice(synth, sample, chan, key, vel);
                    if (voice == NULL) {
                        return FLUID_FAILED;
                    }


                    /* Instrument level, generators */

                    for (i = 0; i < GEN_LAST; i++) {

                        /* SF 2.01 section 9.4 'bullet' 4:
                         *
                         * A generator in a local instrument zone supersedes a
                         * global instrument zone generator.  Both cases supersede
                         * the default generator -> voice_gen_set */

                        if (inst_zone->gen[i].flags){
                            fluid_voice_gen_set(voice, i, inst_zone->gen[i].val);

                        } else if ((global_inst_zone != NULL) && (global_inst_zone->gen[i].flags)) {
                            fluid_voice_gen_set(voice, i, global_inst_zone->gen[i].val);

                        } else {
                            /* The generator has not been defined in this instrument.
                             * Do nothing, leave it at the default.
                             */
                        }

                    } /* for all generators */

                    /* global instrument zone, modulators: Put them all into a
                     * list. */

                    FLUID_MOD_list_count = 0;

                    if (global_inst_zone){
                        mod = global_inst_zone->mod;
                        while (mod){
                            FLUID_MOD_list[FLUID_MOD_list_count++] = mod;
                            mod = mod->next;
                        }
                    }

                    /* local instrument zone, modulators.
                     * Replace modulators with the same definition in the list:
                     * SF 2.01 page 69, 'bullet' 8
                     */
                    mod = inst_zone->mod;

                    while (mod){

                        /* 'Identical' modulators will be deleted by setting their
                         *  list entry to NULL.  The list length is known, NULL
                         *  entries will be ignored later.  SF2.01 section 9.5.1
                         *  page 69, 'bullet' 3 defines 'identical'.  */

                        for (i = 0; i < FLUID_MOD_list_count; i++){
                            if (FLUID_MOD_list[i] && fluid_mod_test_identity(mod,FLUID_MOD_list[i])){
                                FLUID_MOD_list[i] = NULL;
                            }
                        }

                        /* Finally add the new modulator to to the list. */
                        FLUID_MOD_list[FLUID_MOD_list_count++] = mod;
                        mod = mod->next;
                    }

                    /* Add instrument modulators (global / local) to the voice. */
                    for (i = 0; i < FLUID_MOD_list_count; i++){

                        mod = FLUID_MOD_list[i];

                        if (mod != NULL){ /* disabled modulators CANNOT be skipped. */

                            /* Instrument modulators -supersede- existing (default)
                             * modulators.  SF 2.01 page 69, 'bullet' 6 */
                            fluid_voice_add_mod(voice, mod, FLUID_VOICE_OVERWRITE);
                        }
                    }

                    /* Preset level, generators */

                    for (i = 0; i < GEN_LAST; i++) {

                        /* SF 2.01 section 8.5 page 58: If some generators are
                         * encountered at preset level, they should be ignored */
                        if ((i != GEN_STARTADDROFS)
                                && (i != GEN_ENDADDROFS)
                                && (i != GEN_STARTLOOPADDROFS)
                                && (i != GEN_ENDLOOPADDROFS)
                                && (i != GEN_STARTADDRCOARSEOFS)
                                && (i != GEN_ENDADDRCOARSEOFS)
                                && (i != GEN_STARTLOOPADDRCOARSEOFS)
                                && (i != GEN_KEYNUM)
                                && (i != GEN_VELOCITY)
                                && (i != GEN_ENDLOOPADDRCOARSEOFS)
                                && (i != GEN_SAMPLEMODE)
                                && (i != GEN_EXCLUSIVECLASS)
                                && (i != GEN_OVERRIDEROOTKEY)) {

                            /* SF 2.01 section 9.4 'bullet' 9: A generator in a
                             * local preset zone supersedes a global preset zone
                             * generator.  The effect is -added- to the destination
                             * summing node -> voice_gen_incr */

                            if (preset_zone->gen[i].flags) {
                                fluid_voice_gen_incr(voice, i, preset_zone->gen[i].val);
                            } else if ((global_preset_zone != NULL) && global_preset_zone->gen[i].flags) {
                                fluid_voice_gen_incr(voice, i, global_preset_zone->gen[i].val);
                            } else {
                                /* The generator has not been defined in this preset
                                 * Do nothing, leave it unchanged.
                                 */
                            }
                        } /* if available at preset level */
                    } /* for all generators */


                    /* Global preset zone, modulators: put them all into a
                     * list. */
                    FLUID_MOD_list_count = 0;
                    if (global_preset_zone){
                        mod = global_preset_zone->mod;
                        while (mod){
                            FLUID_MOD_list[FLUID_MOD_list_count++] = mod;
                            mod = mod->next;
                        }
                    }

                    /* Process the modulators of the local preset zone.  Kick
                     * out all identical modulators from the global preset zone
                     * (SF 2.01 page 69, second-last bullet) */

                    mod = preset_zone->mod;
                    while (mod){
                        for (i = 0; i < FLUID_MOD_list_count; i++){
                            if (FLUID_MOD_list[i] && fluid_mod_test_identity(mod,FLUID_MOD_list[i])){
                                FLUID_MOD_list[i] = NULL;
                            }
                        }

                        /* Finally add the new modulator to the list. */
                        FLUID_MOD_list[FLUID_MOD_list_count++] = mod;
                        mod = mod->next;
                    }

                    /* Add preset modulators (global / local) to the voice. */
                    for (i = 0; i < FLUID_MOD_list_count; i++){
                        mod = FLUID_MOD_list[i];
                        if ((mod != NULL) && (mod->amount != 0)) { /* disabled modulators can be skipped. */

                            /* Preset modulators -add- to existing instrument /
                             * default modulators.  SF2.01 page 70 first bullet on
                             * page */
                            fluid_voice_add_mod(voice, mod, FLUID_VOICE_ADD);
                        }
                    }

                    /* add the synthesis process to the synthesis loop. */
                    fluid_synth_start_voice(synth, voice);

                    /* Store the ID of the first voice that was created by this noteon event.
                     * Exclusive class may only terminate older voices.
                     * That avoids killing voices, which have just been created.
                     * (a noteon event can create several voice processes with the same exclusive
                     * class - for example when using stereo samples)
                     */
                }

                inst_zone = inst_zone_next(inst_zone);
            }
        }
        preset_zone = preset_zone_next(preset_zone);
    }

    return OK;
}

/*
 * mempreset_set_global_zone
 */
int
mempreset_set_global_zone(mempreset_t* preset, preset_zone_t* zone)
{
    preset->global_zone = zone;
    return OK;
}

/*
 * mempreset_import_sfont
 */
int
mempreset_import_sfont(mempreset_t* preset,
        SFPreset* sfpreset,
        memsfont_t* sfont)
{
    fluid_list_t *p;
    SFZone* sfzone;
    preset_zone_t* zone;
    int count;
    char zone_name[256];
    if ((sfpreset->name != NULL) && (FLUID_STRLEN(sfpreset->name) > 0)) {
        FLUID_STRCPY(preset->name, sfpreset->name);
    } else {
        FLUID_SPRINTF(preset->name, "Bank%d,Preset%d", sfpreset->bank, sfpreset->prenum);
    }
    preset->bank = sfpreset->bank;
    preset->num = sfpreset->prenum;
    p = sfpreset->zone;
    count = 0;
    while (p != NULL) {
        sfzone = (SFZone *) p->data;
        FLUID_SPRINTF(zone_name, "%s/%d", preset->name, count);
        zone = new_preset_zone(zone_name);
        if (zone == NULL) {
            return FLUID_FAILED;
        }
        if (preset_zone_import_sfont(zone, sfzone, sfont) != OK) {
            delete_preset_zone(zone);
            return FLUID_FAILED;
        }
        if ((count == 0) && (preset_zone_get_inst(zone) == NULL)) {
            mempreset_set_global_zone(preset, zone);
        } else if (mempreset_add_zone(preset, zone) != OK) {
            return FLUID_FAILED;
        }
        p = fluid_list_next(p);
        count++;
    }
    return OK;
}

/*
 * mempreset_add_zone
 */
int
mempreset_add_zone(mempreset_t* preset, preset_zone_t* zone)
{
    if (preset->zone == NULL) {
        zone->next = NULL;
        preset->zone = zone;
    } else {
        zone->next = preset->zone;
        preset->zone = zone;
    }
    return OK;
}

/*
 * mempreset_get_zone
 */
preset_zone_t*
mempreset_get_zone(mempreset_t* preset)
{
    return preset->zone;
}

/*
 * mempreset_get_global_zone
 */
preset_zone_t*
mempreset_get_global_zone(mempreset_t* preset)
{
    return preset->global_zone;
}

/*
 * preset_zone_next
 */
preset_zone_t*
preset_zone_next(preset_zone_t* preset)
{
    return preset->next;
}

/*
 * new_preset_zone
 */
preset_zone_t*
new_preset_zone(char *name)
{
    int size;
    preset_zone_t* zone = NULL;
    zone = FLUID_NEW(preset_zone_t);
    if (zone == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    zone->next = NULL;
    size = 1 + FLUID_STRLEN(name);
    zone->name = FLUID_MALLOC(size);
    if (zone->name == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        FLUID_FREE(zone);
        return NULL;
    }
    FLUID_STRCPY(zone->name, name);
    zone->inst = NULL;
    zone->keylo = 0;
    zone->keyhi = 128;
    zone->vello = 0;
    zone->velhi = 128;

    /* Flag all generators as unused (default, they will be set when they are found
     * in the sound font).
     * This also sets the generator values to default, but that is of no concern here.*/
    fluid_gen_set_default_values(&zone->gen[0]);
    zone->mod = NULL; /* list of modulators */
    return zone;
}

/***************************************************************
*
*                           PRESET_ZONE
*/

/*
 * delete_preset_zone
 */
int
delete_preset_zone(preset_zone_t* zone)
{
    fluid_mod_t *mod, *tmp;

    mod = zone->mod;
    while (mod)	/* delete the modulators */
    {
        tmp = mod;
        mod = mod->next;
        fluid_mod_delete (tmp);
    }

    if (zone->name) FLUID_FREE (zone->name);
    if (zone->inst) delete_inst (zone->inst);
    FLUID_FREE(zone);
    return OK;
}

/*
 * preset_zone_import_sfont
 */
int
preset_zone_import_sfont(preset_zone_t* zone, SFZone *sfzone, memsfont_t* sfont)
{
    fluid_list_t *r;
    SFGen* sfgen;
    int count;
    for (count = 0, r = sfzone->gen; r != NULL; count++) {
        sfgen = (SFGen *) r->data;
        switch (sfgen->id) {
            case GEN_KEYRANGE:
                zone->keylo = (int) sfgen->amount.range.lo;
                zone->keyhi = (int) sfgen->amount.range.hi;
                break;
            case GEN_VELRANGE:
                zone->vello = (int) sfgen->amount.range.lo;
                zone->velhi = (int) sfgen->amount.range.hi;
                break;
            default:
                /* FIXME: some generators have an unsigne word amount value but i don't know which ones */
                zone->gen[sfgen->id].val = (fluid_real_t) sfgen->amount.sword;
                zone->gen[sfgen->id].flags = GEN_SET;
                break;
        }
        r = fluid_list_next(r);
    }
    if ((sfzone->instsamp != NULL) && (sfzone->instsamp->data != NULL)) {
        zone->inst = (inst_t*) new_inst();
        if (zone->inst == NULL) {
            FLUID_LOG(FLUID_ERR, "Out of memory");
            return FLUID_FAILED;
        }
        if (inst_import_sfont(zone->inst, (SFInst *) sfzone->instsamp->data, sfont) != OK) {
            return FLUID_FAILED;
        }
    }

    /* Import the modulators (only SF2.1 and higher) */
    for (count = 0, r = sfzone->mod; r != NULL; count++) {

        SFMod* FLUID_MOD_src = (SFMod *)r->data;
        fluid_mod_t * mod_dest = fluid_mod_new();
        int type;

        if (mod_dest == NULL){
            return FLUID_FAILED;
        }
        mod_dest->next = NULL; /* pointer to next modulator, this is the end of the list now.*/

        /* *** Amount *** */
        mod_dest->amount = FLUID_MOD_src->amount;

        /* *** Source *** */
        mod_dest->src1 = FLUID_MOD_src->src & 127; /* index of source 1, seven-bit value, SF2.01 section 8.2, page 50 */
        mod_dest->flags1 = 0;

        /* Bit 7: CC flag SF 2.01 section 8.2.1 page 50*/
        if (FLUID_MOD_src->src & (1<<7)){
            mod_dest->flags1 |= FLUID_MOD_CC;
        } else {
            mod_dest->flags1 |= FLUID_MOD_GC;
        }

        /* Bit 8: D flag SF 2.01 section 8.2.2 page 51*/
        if (FLUID_MOD_src->src & (1<<8)){
            mod_dest->flags1 |= FLUID_MOD_NEGATIVE;
        } else {
            mod_dest->flags1 |= FLUID_MOD_POSITIVE;
        }

        /* Bit 9: P flag SF 2.01 section 8.2.3 page 51*/
        if (FLUID_MOD_src->src & (1<<9)){
            mod_dest->flags1 |= FLUID_MOD_BIPOLAR;
        } else {
            mod_dest->flags1 |= FLUID_MOD_UNIPOLAR;
        }

        /* modulator source types: SF2.01 section 8.2.1 page 52 */
        type=(FLUID_MOD_src->src) >> 10;
        type &= 63; /* type is a 6-bit value */
        if (type == 0){
            mod_dest->flags1 |= FLUID_MOD_LINEAR;
        } else if (type == 1){
            mod_dest->flags1 |= FLUID_MOD_CONCAVE;
        } else if (type == 2){
            mod_dest->flags1 |= FLUID_MOD_CONVEX;
        } else if (type == 3){
            mod_dest->flags1 |= FLUID_MOD_SWITCH;
        } else {
            /* This shouldn't happen - unknown type!
             * Deactivate the modulator by setting the amount to 0. */
            mod_dest->amount=0;
        }

        /* *** Dest *** */
        mod_dest->dest = FLUID_MOD_src->dest; /* index of controlled generator */

        /* *** Amount source *** */
        mod_dest->src2 = FLUID_MOD_src->amtsrc & 127; /* index of source 2, seven-bit value, SF2.01 section 8.2, p.50 */
        mod_dest->flags2 = 0;

        /* Bit 7: CC flag SF 2.01 section 8.2.1 page 50*/
        if (FLUID_MOD_src->amtsrc & (1<<7)){
            mod_dest->flags2 |= FLUID_MOD_CC;
        } else {
            mod_dest->flags2 |= FLUID_MOD_GC;
        }

        /* Bit 8: D flag SF 2.01 section 8.2.2 page 51*/
        if (FLUID_MOD_src->amtsrc & (1<<8)){
            mod_dest->flags2 |= FLUID_MOD_NEGATIVE;
        } else {
            mod_dest->flags2 |= FLUID_MOD_POSITIVE;
        }

        /* Bit 9: P flag SF 2.01 section 8.2.3 page 51*/
        if (FLUID_MOD_src->amtsrc & (1<<9)){
            mod_dest->flags2 |= FLUID_MOD_BIPOLAR;
        } else {
            mod_dest->flags2 |= FLUID_MOD_UNIPOLAR;
        }

        /* modulator source types: SF2.01 section 8.2.1 page 52 */
        type = (FLUID_MOD_src->amtsrc) >> 10;
        type &= 63; /* type is a 6-bit value */
        if (type == 0){
            mod_dest->flags2 |= FLUID_MOD_LINEAR;
        } else if (type == 1){
            mod_dest->flags2 |= FLUID_MOD_CONCAVE;
        } else if (type == 2){
            mod_dest->flags2 |= FLUID_MOD_CONVEX;
        } else if (type == 3){
            mod_dest->flags2 |= FLUID_MOD_SWITCH;
        } else {
            /* This shouldn't happen - unknown type!
             * Deactivate the modulator by setting the amount to 0. */
            mod_dest->amount=0;
        }

        /* *** Transform *** */
        /* SF2.01 only uses the 'linear' transform (0).
         * Deactivate the modulator by setting the amount to 0 in any other case.
         */
        if (FLUID_MOD_src->trans !=0){
            mod_dest->amount = 0;
        }

        /* Store the new modulator in the zone The order of modulators
         * will make a difference, at least in an instrument context: The
         * second modulator overwrites the first one, if they only differ
         * in amount. */
        if (count == 0){
            zone->mod = mod_dest;
        } else {
            fluid_mod_t * last_mod = zone->mod;

            /* Find the end of the list */
            while (last_mod->next != NULL){
                last_mod=last_mod->next;
            }

            last_mod->next = mod_dest;
        }

        r = fluid_list_next(r);
    } /* foreach modulator */

    return OK;
}

/*
 * preset_zone_get_inst
 */
inst_t*
preset_zone_get_inst(preset_zone_t* zone)
{
    return zone->inst;
}

/*
 * preset_zone_inside_range
 */
int
preset_zone_inside_range(preset_zone_t* zone, int key, int vel)
{
    return ((zone->keylo <= key) &&
            (zone->keyhi >= key) &&
            (zone->vello <= vel) &&
            (zone->velhi >= vel));
}

/***************************************************************
*
*                           INST
*/

/*
 * new_inst
 */
inst_t*
new_inst()
{
    inst_t* inst = FLUID_NEW(inst_t);
    if (inst == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    inst->name[0] = 0;
    inst->global_zone = NULL;
    inst->zone = NULL;
    return inst;
}

/*
 * delete_inst
 */
int
delete_inst(inst_t* inst)
{
    inst_zone_t* zone;
    int err = OK;
    if (inst->global_zone != NULL) {
        if (delete_inst_zone(inst->global_zone) != OK) {
            err = FLUID_FAILED;
        }
        inst->global_zone = NULL;
    }
    zone = inst->zone;
    while (zone != NULL) {
        inst->zone = zone->next;
        if (delete_inst_zone(zone) != OK) {
            err = FLUID_FAILED;
        }
        zone = inst->zone;
    }
    FLUID_FREE(inst);
    return err;
}

/*
 * inst_set_global_zone
 */
int
inst_set_global_zone(inst_t* inst, inst_zone_t* zone)
{
    inst->global_zone = zone;
    return OK;
}

/*
 * inst_import_sfont
 */
int
inst_import_sfont(inst_t* inst, SFInst *sfinst, memsfont_t* sfont)
{
    fluid_list_t *p;
    SFZone* sfzone;
    inst_zone_t* zone;
    char zone_name[256];
    int count;

    p = sfinst->zone;
    if ((sfinst->name != NULL) && (FLUID_STRLEN(sfinst->name) > 0)) {
        FLUID_STRCPY(inst->name, sfinst->name);
    } else {
        FLUID_STRCPY(inst->name, "<untitled>");
    }

    count = 0;
    while (p != NULL) {

        sfzone = (SFZone *) p->data;
        FLUID_SPRINTF(zone_name, "%s/%d", inst->name, count);

        zone = new_inst_zone(zone_name);
        if (zone == NULL) {
            return FLUID_FAILED;
        }

        if (inst_zone_import_sfont(zone, sfzone, sfont) != OK) {
            delete_inst_zone(zone);
            return FLUID_FAILED;
        }

        if ((count == 0) && (inst_zone_get_sample(zone) == NULL)) {
            inst_set_global_zone(inst, zone);

        } else if (inst_add_zone(inst, zone) != OK) {
            return FLUID_FAILED;
        }

        p = fluid_list_next(p);
        count++;
    }
    return OK;
}

/*
 * inst_add_zone
 */
int
inst_add_zone(inst_t* inst, inst_zone_t* zone)
{
    if (inst->zone == NULL) {
        zone->next = NULL;
        inst->zone = zone;
    } else {
        zone->next = inst->zone;
        inst->zone = zone;
    }
    return OK;
}

/*
 * inst_get_zone
 */
inst_zone_t*
inst_get_zone(inst_t* inst)
{
    return inst->zone;
}

/*
 * inst_get_global_zone
 */
inst_zone_t*
inst_get_global_zone(inst_t* inst)
{
    return inst->global_zone;
}

/***************************************************************
*
*                           INST_ZONE
*/

/*
 * new_inst_zone
 */
inst_zone_t*
new_inst_zone(char* name)
{
    int size;
    inst_zone_t* zone = NULL;
    zone = FLUID_NEW(inst_zone_t);
    if (zone == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    zone->next = NULL;
    size = 1 + FLUID_STRLEN(name);
    zone->name = FLUID_MALLOC(size);
    if (zone->name == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        FLUID_FREE(zone);
        return NULL;
    }
    FLUID_STRCPY(zone->name, name);
    zone->sample = NULL;
    zone->keylo = 0;
    zone->keyhi = 128;
    zone->vello = 0;
    zone->velhi = 128;

    /* Flag the generators as unused.
     * This also sets the generator values to default, but they will be overwritten anyway, if used.*/
    fluid_gen_set_default_values(&zone->gen[0]);
    zone->mod=NULL; /* list of modulators */
    return zone;
}

/*
 * delete_inst_zone
 */
int
delete_inst_zone(inst_zone_t* zone)
{
    fluid_mod_t *mod, *tmp;

    mod = zone->mod;
    while (mod)	/* delete the modulators */
    {
        tmp = mod;
        mod = mod->next;
        fluid_mod_delete (tmp);
    }

    if (zone->name) FLUID_FREE (zone->name);
    FLUID_FREE(zone);
    return OK;
}

/*
 * inst_zone_next
 */
inst_zone_t*
inst_zone_next(inst_zone_t* zone)
{
    return zone->next;
}

/*
 * inst_zone_import_sfont
 */
int
inst_zone_import_sfont(inst_zone_t* zone, SFZone *sfzone, memsfont_t* sfont)
{
    fluid_list_t *r;
    SFGen* sfgen;
    int count;

    for (count = 0, r = sfzone->gen; r != NULL; count++) {
        sfgen = (SFGen *) r->data;
        switch (sfgen->id) {
            case GEN_KEYRANGE:
                zone->keylo = (int) sfgen->amount.range.lo;
                zone->keyhi = (int) sfgen->amount.range.hi;
                break;
            case GEN_VELRANGE:
                zone->vello = (int) sfgen->amount.range.lo;
                zone->velhi = (int) sfgen->amount.range.hi;
                break;
            default:
                /* FIXME: some generators have an unsigned word amount value but
               i don't know which ones */
                zone->gen[sfgen->id].val = (fluid_real_t) sfgen->amount.sword;
                zone->gen[sfgen->id].flags = GEN_SET;
                break;
        }
        r = fluid_list_next(r);
    }

    /* FIXME */
/*    if (zone->gen[GEN_EXCLUSIVECLASS].flags == GEN_SET) { */
/*      FLUID_LOG(DBG, "ExclusiveClass=%d\n", (int) zone->gen[GEN_EXCLUSIVECLASS].val); */
/*    } */

    /* fixup sample pointer */
    if ((sfzone->instsamp != NULL) && (sfzone->instsamp->data != NULL))
        zone->sample = ((SFSample *)(sfzone->instsamp->data))->fluid_sample;

    /* Import the modulators (only SF2.1 and higher) */
    for (count = 0, r = sfzone->mod; r != NULL; count++) {
        SFMod* FLUID_MOD_src = (SFMod *) r->data;
        int type;
        fluid_mod_t* mod_dest;

        mod_dest = fluid_mod_new();
        if (mod_dest == NULL){
            return FLUID_FAILED;
        }

        mod_dest->next = NULL; /* pointer to next modulator, this is the end of the list now.*/

        /* *** Amount *** */
        mod_dest->amount = FLUID_MOD_src->amount;

        /* *** Source *** */
        mod_dest->src1 = FLUID_MOD_src->src & 127; /* index of source 1, seven-bit value, SF2.01 section 8.2, page 50 */
        mod_dest->flags1 = 0;

        /* Bit 7: CC flag SF 2.01 section 8.2.1 page 50*/
        if (FLUID_MOD_src->src & (1<<7)){
            mod_dest->flags1 |= FLUID_MOD_CC;
        } else {
            mod_dest->flags1 |= FLUID_MOD_GC;
        }

        /* Bit 8: D flag SF 2.01 section 8.2.2 page 51*/
        if (FLUID_MOD_src->src & (1<<8)){
            mod_dest->flags1 |= FLUID_MOD_NEGATIVE;
        } else {
            mod_dest->flags1 |= FLUID_MOD_POSITIVE;
        }

        /* Bit 9: P flag SF 2.01 section 8.2.3 page 51*/
        if (FLUID_MOD_src->src & (1<<9)){
            mod_dest->flags1 |= FLUID_MOD_BIPOLAR;
        } else {
            mod_dest->flags1 |= FLUID_MOD_UNIPOLAR;
        }

        /* modulator source types: SF2.01 section 8.2.1 page 52 */
        type = (FLUID_MOD_src->src) >> 10;
        type &= 63; /* type is a 6-bit value */
        if (type == 0){
            mod_dest->flags1 |= FLUID_MOD_LINEAR;
        } else if (type == 1){
            mod_dest->flags1 |= FLUID_MOD_CONCAVE;
        } else if (type == 2){
            mod_dest->flags1 |= FLUID_MOD_CONVEX;
        } else if (type == 3){
            mod_dest->flags1 |= FLUID_MOD_SWITCH;
        } else {
            /* This shouldn't happen - unknown type!
             * Deactivate the modulator by setting the amount to 0. */
            mod_dest->amount = 0;
        }

        /* *** Dest *** */
        mod_dest->dest=FLUID_MOD_src->dest; /* index of controlled generator */

        /* *** Amount source *** */
        mod_dest->src2=FLUID_MOD_src->amtsrc & 127; /* index of source 2, seven-bit value, SF2.01 section 8.2, page 50 */
        mod_dest->flags2 = 0;

        /* Bit 7: CC flag SF 2.01 section 8.2.1 page 50*/
        if (FLUID_MOD_src->amtsrc & (1<<7)){
            mod_dest->flags2 |= FLUID_MOD_CC;
        } else {
            mod_dest->flags2 |= FLUID_MOD_GC;
        }

        /* Bit 8: D flag SF 2.01 section 8.2.2 page 51*/
        if (FLUID_MOD_src->amtsrc & (1<<8)){
            mod_dest->flags2 |= FLUID_MOD_NEGATIVE;
        } else {
            mod_dest->flags2 |= FLUID_MOD_POSITIVE;
        }

        /* Bit 9: P flag SF 2.01 section 8.2.3 page 51*/
        if (FLUID_MOD_src->amtsrc & (1<<9)){
            mod_dest->flags2 |= FLUID_MOD_BIPOLAR;
        } else {
            mod_dest->flags2 |= FLUID_MOD_UNIPOLAR;
        }

        /* modulator source types: SF2.01 section 8.2.1 page 52 */
        type=(FLUID_MOD_src->amtsrc) >> 10;
        type &= 63; /* type is a 6-bit value */
        if (type == 0){
            mod_dest->flags2 |= FLUID_MOD_LINEAR;
        } else if (type == 1){
            mod_dest->flags2 |= FLUID_MOD_CONCAVE;
        } else if (type == 2){
            mod_dest->flags2 |= FLUID_MOD_CONVEX;
        } else if (type == 3){
            mod_dest->flags2 |= FLUID_MOD_SWITCH;
        } else {
            /* This shouldn't happen - unknown type!
             * Deactivate the modulator by setting the amount to 0. */
            mod_dest->amount = 0;
        }

        /* *** Transform *** */
        /* SF2.01 only uses the 'linear' transform (0).
         * Deactivate the modulator by setting the amount to 0 in any other case.
         */
        if (FLUID_MOD_src->trans !=0){
            mod_dest->amount = 0;
        }

        /* Store the new modulator in the zone
         * The order of modulators will make a difference, at least in an instrument context:
         * The second modulator overwrites the first one, if they only differ in amount. */
        if (count == 0){
            zone->mod=mod_dest;
        } else {
            fluid_mod_t * last_mod=zone->mod;
            /* Find the end of the list */
            while (last_mod->next != NULL){
                last_mod=last_mod->next;
            }
            last_mod->next=mod_dest;
        }

        r = fluid_list_next(r);
    } /* foreach modulator */
    return OK;
}

/*
 * inst_zone_get_sample
 */
fluid_sample_t*
inst_zone_get_sample(inst_zone_t* zone)
{
    return zone->sample;
}

/*
 * inst_zone_inside_range
 */
int
inst_zone_inside_range(inst_zone_t* zone, int key, int vel)
{
    return ((zone->keylo <= key) &&
            (zone->keyhi >= key) &&
            (zone->vello <= vel) &&
            (zone->velhi >= vel));
}

/***************************************************************
*
*                           SAMPLE
*/

/*
 * new_sample
 */
fluid_sample_t*
new_sample()
{
    fluid_sample_t* sample = NULL;

    sample = FLUID_NEW(fluid_sample_t);
    if (sample == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }

    memset(sample, 0, sizeof(fluid_sample_t));
    sample->valid = 1;

    return sample;
}

/*
 * delete_sample
 */
int
delete_sample(fluid_sample_t* sample)
{
    FLUID_FREE(sample);
    return OK;
}

/*
 * sample_in_rom
 */
int
sample_in_rom(fluid_sample_t* sample)
{
    return (sample->sampletype & FLUID_SAMPLETYPE_ROM);
}

/*
 * sample_import_sfont
 */
int
sample_import_sfont(fluid_sample_t* sample, SFSample* sfsample, memsfont_t* sfont)
{
    FLUID_STRCPY(sample->name, sfsample->name);
    sample->data = sfont->sampledata;
    sample->start = sfsample->start;
    sample->end = sfsample->start + sfsample->end;
    sample->loopstart = sfsample->start + sfsample->loopstart;
    sample->loopend = sfsample->start + sfsample->loopend;
    sample->samplerate = sfsample->samplerate;
    sample->origpitch = sfsample->origpitch;
    sample->pitchadj = sfsample->pitchadj;
    sample->sampletype = sfsample->sampletype;

    if (sample->sampletype & FLUID_SAMPLETYPE_ROM) {
        sample->valid = 0;
        FLUID_LOG(FLUID_WARN, "Ignoring sample %s: can't use ROM samples", sample->name);
    }
    if (sample->end - sample->start < 8) {
        sample->valid = 0;
        FLUID_LOG(FLUID_WARN, "Ignoring sample %s: too few sample data points", sample->name);
    } else {
/*      if (sample->loopstart < sample->start + 8) { */
/*        FLUID_LOG(FLUID_WARN, "Fixing sample %s: at least 8 data points required before loop start", sample->name);     */
/*        sample->loopstart = sample->start + 8; */
/*      } */
/*      if (sample->loopend > sample->end - 8) { */
/*        FLUID_LOG(FLUID_WARN, "Fixing sample %s: at least 8 data points required after loop end", sample->name);     */
/*        sample->loopend = sample->end - 8; */
/*      } */
    }
    return OK;
}



/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/
/********************************************************************************/



/*=================================sfload.c========================
  Borrowed from Smurf SoundFont Editor by Josh Green
  =================================================================*/

/*
   functions for loading data from sfont files, with appropriate byte swapping
   on big endian machines. Sfont IDs are not swapped because the ID read is
   equivalent to the matching ID list in memory regardless of LE/BE machine
*/

#if defined(__BYTE_ORDER__)&&(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

#define READCHUNK(var,fd)	G_STMT_START {		\
	if (!mem_safe_fread(var, 8, data))			\
		return(FAIL);				\
	((SFChunk *)(var))->size = GUINT32_FROM_LE(((SFChunk *)(var))->size);  \
} G_STMT_END

#define READD(var,fd)		G_STMT_START {		\
	unsigned int _temp;				\
	if (!mem_safe_fread(&_temp, 4, data))			\
		return(FAIL);				\
	var = GINT32_FROM_LE(_temp);			\
} G_STMT_END

#define READW(var,fd)		G_STMT_START {		\
	unsigned short _temp;				\
	if (!mem_safe_fread(&_temp, 2, data))			\
		return(FAIL);				\
	var = GINT16_FROM_LE(_temp);			\
} G_STMT_END

#else

#define READCHUNK(var,data)	G_STMT_START {		\
    if (!mem_safe_fread(var, 8, data))			\
	return(FAIL);					\
    ((SFChunk *)(var))->size = GUINT32_FROM_LE(((SFChunk *)(var))->size);  \
} G_STMT_END

#define READD(var,data)		G_STMT_START {		\
    unsigned int _temp;					\
    if (!mem_safe_fread(&_temp, 4, data))			\
	return(FAIL);					\
    var = GINT32_FROM_LE(_temp);			\
} G_STMT_END

#define READW(var,data)		G_STMT_START {		\
    unsigned short _temp;					\
    if (!mem_safe_fread(&_temp, 2, data))			\
	return(FAIL);					\
    var = GINT16_FROM_LE(_temp);			\
} G_STMT_END

#endif


#define READID(var,data)		G_STMT_START {		\
    if (!mem_safe_fread(var, 4, data))			\
	return(FAIL);					\
} G_STMT_END

#define READSTR(var,data)		G_STMT_START {		\
    if (!mem_safe_fread(var, 20, data))			\
	return(FAIL);					\
    (*var)[20] = '\0';					\
} G_STMT_END

#define READB(var,data)		G_STMT_START {		\
    if (!mem_safe_fread(&var, 1, data))			\
	return(FAIL);					\
} G_STMT_END

#define FSKIP(size,data)		G_STMT_START {		\
    if (!mem_safe_fseek(data, size, SEEK_CUR))		\
	return(FAIL);					\
} G_STMT_END

#define FSKIPW(data)		G_STMT_START {		\
    if (!mem_safe_fseek(data, 2, SEEK_CUR))			\
	return(FAIL);					\
} G_STMT_END

/* removes and advances a fluid_list_t pointer */
#define SLADVREM(list, item)	G_STMT_START {		\
    fluid_list_t *_temp = item;				\
    item = fluid_list_next(item);				\
    list = fluid_list_remove_link(list, _temp);		\
    delete1_fluid_list(_temp);				\
} G_STMT_END

static int mem_chunkid (unsigned int id);
static int mem_load_body (unsigned int size, SFData * sf, const void ** data);
static int mem_read_listchunk (SFChunk * chunk, const void ** data);
static int mem_process_info (int size, SFData * sf, const void ** data);
static int mem_process_sdta (unsigned int size, SFData * sf, const void ** data, long base_addr);
static int mem_pdtahelper (unsigned int expid, unsigned int reclen, SFChunk * chunk,
        int * size, const void ** data);
static int mem_process_pdta (int size, SFData * sf, const void ** data);
static int mem_load_phdr (int size, SFData * sf, const void ** data);
static int mem_load_pbag (int size, SFData * sf, const void ** data);
static int mem_load_pmod (int size, SFData * sf, const void ** data);
static int mem_load_pgen (int size, SFData * sf, const void ** data);
static int mem_load_ihdr (int size, SFData * sf, const void ** data);
static int mem_load_ibag (int size, SFData * sf, const void ** data);
static int mem_load_imod (int size, SFData * sf, const void ** data);
static int mem_load_igen (int size, SFData * sf, const void ** data);
static int mem_load_shdr (unsigned int size, SFData * sf, const void ** data);
static int mem_fixup_pgen (SFData * sf);
static int mem_fixup_igen (SFData * sf);
static int mem_fixup_sample (SFData * sf);

char mem_idlist[] = {
        "RIFFLISTsfbkINFOsdtapdtaifilisngINAMiromiverICRDIENGIPRD"
                "ICOPICMTISFTsnamsmplphdrpbagpmodpgeninstibagimodigenshdr"
};

static unsigned int sdtachunk_size;

/* sound font file load functions */
static int
mem_chunkid (unsigned int id)
{
    unsigned int i;
    unsigned int *p;

    p = (unsigned int *) & mem_idlist;
    for (i = 0; i < sizeof (mem_idlist) / sizeof (int); i++, p += 1)
        if (*p == id)
            return (i + 1);

    return (UNKN_ID);
}

SFData *
sfload_mem (const void * data)
{
    SFData *sf = NULL;
//    const void *fd;
    int fsize = 0;
    int err = FALSE;

//    if (!(fd = fopen (fname, "rb")))
//    {
//        FLUID_LOG (FLUID_ERR, _("Unable to open file \"%s\""), fname);
//        return (NULL);
//    }
//
    if (!(sf = FLUID_NEW (SFData)))
    {
        FLUID_LOG(FLUID_ERR, "Out of memory");
//        fclose(fd);
        err = TRUE;
    }

    if (!err)
    {
        memset (sf, 0, sizeof (SFData));	/* zero sfdata */
//        sf->fname = FLUID_STRDUP (fname);	/* copy file name */
//        sf->sffd = fd;
    }

    /* get size of file */
//    if (!err && fseek (fd, 0L, SEEK_END) == -1)
//    {				/* seek to end of file */
//        err = TRUE;
//        FLUID_LOG (FLUID_ERR, _("Seek to end of file failed"));
//    }
//    if (!err && (fsize = ftell (fd)) == -1)
//    {				/* position = size */
//        err = TRUE;
//        FLUID_LOG (FLUID_ERR, _("Get end of file position failed"));
//    }
//    if (!err)
//        rewind (fd);

    if (!err && !mem_load_body (fsize, sf, &data))
        err = TRUE;			/* load the sfont */

    if (err)
    {
        if (sf)
            mem_sfont_close (sf);
        return (NULL);
    }

    return (sf);
}

static int
mem_load_body (unsigned int size, SFData * sf, const void ** data)
{
    SFChunk chunk;

    long base_addr = (long) *data;

    READCHUNK (&chunk, data);	/* load RIFF chunk */
    if (mem_chunkid (chunk.id) != RIFF_ID) {	/* error if not RIFF */
        FLUID_LOG (FLUID_ERR, _("Not a RIFF file"));
        return (FAIL);
    }

    READID (&chunk.id, data);	/* load file ID */
    if (mem_chunkid (chunk.id) != SFBK_ID) {	/* error if not SFBK_ID */
        FLUID_LOG (FLUID_ERR, _("Not a SoundFont file"));
        return (FAIL);
    }

//    if (chunk.size != size - 8) {
//        mem_gerr (ErrCorr, _("SoundFont file size mismatch"));
//        return (FAIL);
//    }

    /* Process INFO block */
    if (!mem_read_listchunk (&chunk, data))
        return (FAIL);
    if (mem_chunkid (chunk.id) != INFO_ID)
        return (mem_gerr (ErrCorr, _("Invalid ID found when expecting INFO chunk")));
    if (!mem_process_info (chunk.size, sf, data))
        return (FAIL);

    /* Process sample chunk */
    if (!mem_read_listchunk (&chunk, data))
        return (FAIL);
    if (mem_chunkid (chunk.id) != SDTA_ID)
        return (mem_gerr (ErrCorr,
                _("Invalid ID found when expecting SAMPLE chunk")));
    if (!mem_process_sdta (chunk.size, sf, data, base_addr))
        return (FAIL);

    /* process HYDRA chunk */
    if (!mem_read_listchunk (&chunk, data))
        return (FAIL);
    if (mem_chunkid (chunk.id) != PDTA_ID)
        return (mem_gerr (ErrCorr, _("Invalid ID found when expecting HYDRA chunk")));
    if (!mem_process_pdta (chunk.size, sf, data))
        return (FAIL);

    if (!mem_fixup_pgen (sf))
        return (FAIL);
    if (!mem_fixup_igen (sf))
        return (FAIL);
    if (!mem_fixup_sample (sf))
        return (FAIL);

    /* sort preset list by bank, preset # */
    sf->preset = fluid_list_sort (sf->preset,
            (fluid_compare_func_t) mem_sfont_preset_compare_func);

    return (OK);
}

static int
mem_read_listchunk (SFChunk * chunk, const void ** data)
{
    READCHUNK (chunk, data);	/* read list chunk */
    if (mem_chunkid (chunk->id) != LIST_ID)	/* error if ! list chunk */
        return (mem_gerr (ErrCorr, _("Invalid chunk id in level 0 parse")));
    READID (&chunk->id, data);	/* read id string */
    chunk->size -= 4;
    return (OK);
}

static int
mem_process_info (int size, SFData * sf, const void ** data)
{
    SFChunk chunk;
    unsigned char id;
    char *item;
    unsigned short ver;

    while (size > 0)
    {
        READCHUNK (&chunk, data);
        size -= 8;

        id = mem_chunkid (chunk.id);

        if (id == IFIL_ID)
        {			/* sound font version chunk? */
            if (chunk.size != 4)
                return (mem_gerr (ErrCorr,
                        _("Sound font version info chunk has invalid size")));

            READW (ver, data);
            sf->version.major = ver;
            READW (ver, data);
            sf->version.minor = ver;

            if (sf->version.major < 2) {
                FLUID_LOG (FLUID_ERR,
                        _("Sound font version is %d.%d which is not"
                                " supported, convert to version 2.0x"),
                        sf->version.major,
                        sf->version.minor);
                return (FAIL);
            }

            if (sf->version.major > 2) {
                FLUID_LOG (FLUID_WARN,
                        _("Sound font version is %d.%d which is newer than"
                                " what this version of FLUID Synth was designed for (v2.0x)"),
                        sf->version.major,
                        sf->version.minor);
                return (FAIL);
            }
        }
        else if (id == IVER_ID)
        {			/* ROM version chunk? */
            if (chunk.size != 4)
                return (mem_gerr (ErrCorr,
                        _("ROM version info chunk has invalid size")));

            READW (ver, data);
            sf->romver.major = ver;
            READW (ver, data);
            sf->romver.minor = ver;
        }
        else if (id != UNKN_ID)
        {
            if ((id != ICMT_ID && chunk.size > 256) || (chunk.size > 65536)
                    || (chunk.size % 2))
                return (mem_gerr (ErrCorr,
                        _("INFO sub chunk %.4s has invalid chunk size"
                                " of %d bytes"), &chunk.id, chunk.size));

            /* alloc for chunk id and da chunk */
            if (!(item = FLUID_MALLOC (chunk.size + 1)))
            {
                FLUID_LOG(FLUID_ERR, "Out of memory");
                return (FAIL);
            }

            /* attach to INFO list, sfont_close will cleanup if FAIL occurs */
            sf->info = fluid_list_append (sf->info, item);

            *(unsigned char *) item = id;
            if (!mem_safe_fread (&item[1], chunk.size, data))
                return (FAIL);

            /* force terminate info item (don't forget uint8 info ID) */
            *(item + chunk.size) = '\0';
        }
        else
            return (mem_gerr (ErrCorr, _("Invalid chunk id in INFO chunk")));
        size -= chunk.size;
    }

    if (size < 0)
        return (mem_gerr (ErrCorr, _("INFO chunk size mismatch")));

    return (OK);
}

static int
mem_process_sdta (unsigned int size, SFData * sf, const void ** data, long base_addr)
{
    SFChunk chunk;

    if (size == 0)
        return (OK);		/* no sample data? */

    /* read sub chunk */
    READCHUNK (&chunk, data);
    size -= 8;

    if (mem_chunkid (chunk.id) != SMPL_ID)
        return (mem_gerr (ErrCorr,
                _("Expected SMPL chunk found invalid id instead")));

    /* SDTA chunk may also contain sm24 chunk for 24 bit samples
     * (not yet supported), only an error if SMPL chunk size is
     * greater than SDTA. */
    if (chunk.size > size)
        return (mem_gerr (ErrCorr, _("SDTA chunk size mismatch")));

    /* sample data follows */
    long position = (long)*data - base_addr;
    sf->samplepos = position;//ftell (data);

    /* used in mem_fixup_sample() to check validity of sample headers */
    sdtachunk_size = chunk.size;
    sf->samplesize = chunk.size;

    FSKIP (size, data);

    return (OK);
}

static int
mem_pdtahelper (unsigned int expid, unsigned int reclen, SFChunk * chunk,
        int * size, const void ** data)
{
    unsigned int id;
    char *expstr;

    expstr = CHNKIDSTR (expid);	/* in case we need it */

    READCHUNK (chunk, data);
    *size -= 8;

    if ((id = mem_chunkid (chunk->id)) != expid)
        return (mem_gerr (ErrCorr, _("Expected"
                " PDTA sub-chunk \"%.4s\" found invalid id instead"), expstr));

    if (chunk->size % reclen)	/* valid chunk size? */
        return (mem_gerr (ErrCorr,
                _("\"%.4s\" chunk size is not a multiple of %d bytes"), expstr,
                reclen));
    if ((*size -= chunk->size) < 0)
        return (mem_gerr (ErrCorr,
                _("\"%.4s\" chunk size exceeds remaining PDTA chunk size"), expstr));
    return (OK);
}

static int
mem_process_pdta (int size, SFData * sf, const void ** data)
{
    SFChunk chunk;

    if (!mem_pdtahelper (PHDR_ID, SFPHDRSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_phdr (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (PBAG_ID, SFBAGSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_pbag (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (PMOD_ID, SFMODSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_pmod (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (PGEN_ID, SFGENSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_pgen (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (IHDR_ID, SFIHDRSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_ihdr (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (IBAG_ID, SFBAGSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_ibag (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (IMOD_ID, SFMODSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_imod (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (IGEN_ID, SFGENSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_igen (chunk.size, sf, data))
        return (FAIL);

    if (!mem_pdtahelper (SHDR_ID, SFSHDRSIZE, &chunk, &size, data))
        return (FAIL);
    if (!mem_load_shdr (chunk.size, sf, data))
        return (FAIL);

    return (OK);
}

/* preset header loader */
static int
mem_load_phdr (int size, SFData * sf, const void ** data)
{
    int i, i2;
    SFPreset *p, *pr = NULL;	/* ptr to current & previous preset */
    unsigned short zndx, pzndx = 0;

    if (size % SFPHDRSIZE || size == 0)
        return (mem_gerr (ErrCorr, _("Preset header chunk size is invalid")));

    i = size / SFPHDRSIZE - 1;
    if (i == 0)
    {				/* at least one preset + term record */
        FLUID_LOG (FLUID_WARN, _("File contains no presets"));
        FSKIP (SFPHDRSIZE, data);
        return (OK);
    }

    for (; i > 0; i--)
    {				/* load all preset headers */
        p = FLUID_NEW (SFPreset);
        sf->preset = fluid_list_append (sf->preset, p);
        p->zone = NULL;		/* In case of failure, sfont_close can cleanup */
        READSTR (&p->name, data);	/* possible read failure ^ */
        READW (p->prenum, data);
        READW (p->bank, data);
        READW (zndx, data);
        READD (p->libr, data);
        READD (p->genre, data);
        READD (p->morph, data);

        if (pr)
        {			/* not first preset? */
            if (zndx < pzndx)
                return (mem_gerr (ErrCorr, _("Preset header indices not monotonic")));
            i2 = zndx - pzndx;
            while (i2--)
            {
                pr->zone = fluid_list_prepend (pr->zone, NULL);
            }
        }
        else if (zndx > 0)	/* 1st preset, warn if ofs >0 */
            FLUID_LOG (FLUID_WARN, _("%d preset zones not referenced, discarding"), zndx);
        pr = p;			/* update preset ptr */
        pzndx = zndx;
    }

    FSKIP (24, data);
    READW (zndx, data);		/* Read terminal generator index */
    FSKIP (12, data);

    if (zndx < pzndx)
        return (mem_gerr (ErrCorr, _("Preset header indices not monotonic")));
    i2 = zndx - pzndx;
    while (i2--)
    {
        pr->zone = fluid_list_prepend (pr->zone, NULL);
    }

    return (OK);
}

/* preset bag loader */
static int
mem_load_pbag (int size, SFData * sf, const void ** data)
{
    fluid_list_t *p, *p2;
    SFZone *z, *pz = NULL;
    unsigned short genndx, modndx;
    unsigned short pgenndx = 0, pmodndx = 0;
    unsigned short i;

    if (size % SFBAGSIZE || size == 0)	/* size is multiple of SFBAGSIZE? */
        return (mem_gerr (ErrCorr, _("Preset bag chunk size is invalid")));

    p = sf->preset;
    while (p)
    {				/* traverse through presets */
        p2 = ((SFPreset *) (p->data))->zone;
        while (p2)
        {			/* traverse preset's zones */
            if ((size -= SFBAGSIZE) < 0)
                return (mem_gerr (ErrCorr, _("Preset bag chunk size mismatch")));
            z = FLUID_NEW (SFZone);
            p2->data = z;
            z->gen = NULL;	/* Init gen and mod before possible failure, */
            z->mod = NULL;	/* to ensure proper cleanup (sfont_close) */
            READW (genndx, data);	/* possible read failure ^ */
            READW (modndx, data);
            z->instsamp = NULL;

            if (pz)
            {			/* if not first zone */
                if (genndx < pgenndx)
                    return (mem_gerr (ErrCorr,
                            _("Preset bag generator indices not monotonic")));
                if (modndx < pmodndx)
                    return (mem_gerr (ErrCorr,
                            _("Preset bag modulator indices not monotonic")));
                i = genndx - pgenndx;
                while (i--)
                    pz->gen = fluid_list_prepend (pz->gen, NULL);
                i = modndx - pmodndx;
                while (i--)
                    pz->mod = fluid_list_prepend (pz->mod, NULL);
            }
            pz = z;		/* update previous zone ptr */
            pgenndx = genndx;	/* update previous zone gen index */
            pmodndx = modndx;	/* update previous zone mod index */
            p2 = fluid_list_next (p2);
        }
        p = fluid_list_next (p);
    }

    size -= SFBAGSIZE;
    if (size != 0)
        return (mem_gerr (ErrCorr, _("Preset bag chunk size mismatch")));

    READW (genndx, data);
    READW (modndx, data);

    if (!pz)
    {
        if (genndx > 0)
            FLUID_LOG (FLUID_WARN, _("No preset generators and terminal index not 0"));
        if (modndx > 0)
            FLUID_LOG (FLUID_WARN, _("No preset modulators and terminal index not 0"));
        return (OK);
    }

    if (genndx < pgenndx)
        return (mem_gerr (ErrCorr, _("Preset bag generator indices not monotonic")));
    if (modndx < pmodndx)
        return (mem_gerr (ErrCorr, _("Preset bag modulator indices not monotonic")));
    i = genndx - pgenndx;
    while (i--)
        pz->gen = fluid_list_prepend (pz->gen, NULL);
    i = modndx - pmodndx;
    while (i--)
        pz->mod = fluid_list_prepend (pz->mod, NULL);

    return (OK);
}

/* preset modulator loader */
static int
mem_load_pmod (int size, SFData * sf, const void ** data)
{
    fluid_list_t *p, *p2, *p3;
    SFMod *m;

    p = sf->preset;
    while (p)
    {				/* traverse through all presets */
        p2 = ((SFPreset *) (p->data))->zone;
        while (p2)
        {			/* traverse this preset's zones */
            p3 = ((SFZone *) (p2->data))->mod;
            while (p3)
            {			/* load zone's modulators */
                if ((size -= SFMODSIZE) < 0)
                    return (mem_gerr (ErrCorr,
                            _("Preset modulator chunk size mismatch")));
                m = FLUID_NEW (SFMod);
                p3->data = m;
                READW (m->src, data);
                READW (m->dest, data);
                READW (m->amount, data);
                READW (m->amtsrc, data);
                READW (m->trans, data);
                p3 = fluid_list_next (p3);
            }
            p2 = fluid_list_next (p2);
        }
        p = fluid_list_next (p);
    }

    /*
       If there isn't even a terminal record
       Hmmm, the specs say there should be one, but..
     */
    if (size == 0)
        return (OK);

    size -= SFMODSIZE;
    if (size != 0)
        return (mem_gerr (ErrCorr, _("Preset modulator chunk size mismatch")));
    FSKIP (SFMODSIZE, data);	/* terminal mod */

    return (OK);
}

/* -------------------------------------------------------------------
 * preset generator loader
 * generator (per preset) loading rules:
 * Zones with no generators or modulators shall be annihilated
 * Global zone must be 1st zone, discard additional ones (instrumentless zones)
 *
 * generator (per zone) loading rules (in order of decreasing precedence):
 * KeyRange is 1st in list (if exists), else discard
 * if a VelRange exists only preceded by a KeyRange, else discard
 * if a generator follows an instrument discard it
 * if a duplicate generator exists replace previous one
 * ------------------------------------------------------------------- */
static int
mem_load_pgen (int size, SFData * sf, const void ** data)
{
    fluid_list_t *p, *p2, *p3, *dup, **hz = NULL;
    SFZone *z;
    SFGen *g;
    SFGenAmount genval;
    unsigned short genid;
    int level, skip, drop, gzone, discarded;

    p = sf->preset;
    while (p)
    {				/* traverse through all presets */
        gzone = FALSE;
        discarded = FALSE;
        p2 = ((SFPreset *) (p->data))->zone;
        if (p2)
            hz = &p2;
        while (p2)
        {			/* traverse preset's zones */
            level = 0;
            z = (SFZone *) (p2->data);
            p3 = z->gen;
            while (p3)
            {			/* load zone's generators */
                dup = NULL;
                skip = FALSE;
                drop = FALSE;
                if ((size -= SFGENSIZE) < 0)
                    return (mem_gerr (ErrCorr,
                            _("Preset generator chunk size mismatch")));

                READW (genid, data);

                if (genid == Gen_KeyRange)
                {		/* nothing precedes */
                    if (level == 0)
                    {
                        level = 1;
                        READB (genval.range.lo, data);
                        READB (genval.range.hi, data);
                    }
                    else
                        skip = TRUE;
                }
                else if (genid == Gen_VelRange)
                {		/* only KeyRange precedes */
                    if (level <= 1)
                    {
                        level = 2;
                        READB (genval.range.lo, data);
                        READB (genval.range.hi, data);
                    }
                    else
                        skip = TRUE;
                }
                else if (genid == Gen_Instrument)
                {		/* inst is last gen */
                    level = 3;
                    READW (genval.uword, data);
                    ((SFZone *) (p2->data))->instsamp = (void *)(genval.uword + 1);
                    break;	/* break out of generator loop */
                }
                else
                {
                    level = 2;
                    if (mem_gen_validp (genid))
                    {		/* generator valid? */
                        READW (genval.sword, data);
                        dup = mem_gen_inlist (genid, z->gen);
                    }
                    else
                        skip = TRUE;
                }

                if (!skip)
                {
                    if (!dup)
                    {		/* if gen ! dup alloc new */
                        g = FLUID_NEW (SFGen);
                        p3->data = g;
                        g->id = genid;
                    }
                    else
                    {
                        g = (SFGen *) (dup->data);	/* ptr to orig gen */
                        drop = TRUE;
                    }
                    g->amount = genval;
                }
                else
                {		/* Skip this generator */
                    discarded = TRUE;
                    drop = TRUE;
                    FSKIPW (data);
                }

                if (!drop)
                    p3 = fluid_list_next (p3);	/* next gen */
                else
                SLADVREM (z->gen, p3);	/* drop place holder */

            }			/* generator loop */

            if (level == 3)
            SLADVREM (z->gen, p3);	/* zone has inst? */
            else
            {			/* congratulations its a global zone */
                if (!gzone)
                {		/* Prior global zones? */
                    gzone = TRUE;

                    /* if global zone is not 1st zone, relocate */
                    if (*hz != p2)
                    {
                        void* save = p2->data;
                        FLUID_LOG (FLUID_WARN,
                                _("Preset \"%s\": Global zone is not first zone"),
                                ((SFPreset *) (p->data))->name);
                        SLADVREM (*hz, p2);
                        *hz = fluid_list_prepend (*hz, save);
                        continue;
                    }
                }
                else
                {		/* previous global zone exists, discard */
                    FLUID_LOG (FLUID_WARN,
                            _("Preset \"%s\": Discarding invalid global zone"),
                            ((SFPreset *) (p->data))->name);
                    mem_sfont_zone_delete (sf, hz, (SFZone *) (p2->data));
                }
            }

            while (p3)
            {			/* Kill any zones following an instrument */
                discarded = TRUE;
                if ((size -= SFGENSIZE) < 0)
                    return (mem_gerr (ErrCorr,
                            _("Preset generator chunk size mismatch")));
                FSKIP (SFGENSIZE, data);
                SLADVREM (z->gen, p3);
            }

            p2 = fluid_list_next (p2);	/* next zone */
        }
        if (discarded)
            FLUID_LOG(FLUID_WARN,
                    _("Preset \"%s\": Some invalid generators were discarded"),
                    ((SFPreset *) (p->data))->name);
        p = fluid_list_next (p);
    }

    /* in case there isn't a terminal record */
    if (size == 0)
        return (OK);

    size -= SFGENSIZE;
    if (size != 0)
        return (mem_gerr (ErrCorr, _("Preset generator chunk size mismatch")));
    FSKIP (SFGENSIZE, data);	/* terminal gen */

    return (OK);
}

/* instrument header loader */
static int
mem_load_ihdr (int size, SFData * sf, const void ** data)
{
    int i, i2;
    SFInst *p, *pr = NULL;	/* ptr to current & previous instrument */
    unsigned short zndx, pzndx = 0;

    if (size % SFIHDRSIZE || size == 0)	/* chunk size is valid? */
        return (mem_gerr (ErrCorr, _("Instrument header has invalid size")));

    size = size / SFIHDRSIZE - 1;
    if (size == 0)
    {				/* at least one preset + term record */
        FLUID_LOG (FLUID_WARN, _("File contains no instruments"));
        FSKIP (SFIHDRSIZE, data);
        return (OK);
    }

    for (i = 0; i < size; i++)
    {				/* load all instrument headers */
        p = FLUID_NEW (SFInst);
        sf->inst = fluid_list_append (sf->inst, p);
        p->zone = NULL;		/* For proper cleanup if fail (sfont_close) */
        READSTR (&p->name, data);	/* Possible read failure ^ */
        READW (zndx, data);

        if (pr)
        {			/* not first instrument? */
            if (zndx < pzndx)
                return (mem_gerr (ErrCorr,
                        _("Instrument header indices not monotonic")));
            i2 = zndx - pzndx;
            while (i2--)
                pr->zone = fluid_list_prepend (pr->zone, NULL);
        }
        else if (zndx > 0)	/* 1st inst, warn if ofs >0 */
            FLUID_LOG (FLUID_WARN, _("%d instrument zones not referenced, discarding"),
                    zndx);
        pzndx = zndx;
        pr = p;			/* update instrument ptr */
    }

    FSKIP (20, data);
    READW (zndx, data);

    if (zndx < pzndx)
        return (mem_gerr (ErrCorr, _("Instrument header indices not monotonic")));
    i2 = zndx - pzndx;
    while (i2--)
        pr->zone = fluid_list_prepend (pr->zone, NULL);

    return (OK);
}

/* instrument bag loader */
static int
mem_load_ibag (int size, SFData * sf, const void ** data)
{
    fluid_list_t *p, *p2;
    SFZone *z, *pz = NULL;
    unsigned short genndx, modndx, pgenndx = 0, pmodndx = 0;
    int i;

    if (size % SFBAGSIZE || size == 0)	/* size is multiple of SFBAGSIZE? */
        return (mem_gerr (ErrCorr, _("Instrument bag chunk size is invalid")));

    p = sf->inst;
    while (p)
    {				/* traverse through inst */
        p2 = ((SFInst *) (p->data))->zone;
        while (p2)
        {			/* load this inst's zones */
            if ((size -= SFBAGSIZE) < 0)
                return (mem_gerr (ErrCorr, _("Instrument bag chunk size mismatch")));
            z = FLUID_NEW (SFZone);
            p2->data = z;
            z->gen = NULL;	/* In case of failure, */
            z->mod = NULL;	/* sfont_close can clean up */
            READW (genndx, data);	/* READW = possible read failure */
            READW (modndx, data);
            z->instsamp = NULL;

            if (pz)
            {			/* if not first zone */
                if (genndx < pgenndx)
                    return (mem_gerr (ErrCorr,
                            _("Instrument generator indices not monotonic")));
                if (modndx < pmodndx)
                    return (mem_gerr (ErrCorr,
                            _("Instrument modulator indices not monotonic")));
                i = genndx - pgenndx;
                while (i--)
                    pz->gen = fluid_list_prepend (pz->gen, NULL);
                i = modndx - pmodndx;
                while (i--)
                    pz->mod = fluid_list_prepend (pz->mod, NULL);
            }
            pz = z;		/* update previous zone ptr */
            pgenndx = genndx;
            pmodndx = modndx;
            p2 = fluid_list_next (p2);
        }
        p = fluid_list_next (p);
    }

    size -= SFBAGSIZE;
    if (size != 0)
        return (mem_gerr (ErrCorr, _("Instrument chunk size mismatch")));

    READW (genndx, data);
    READW (modndx, data);

    if (!pz)
    {				/* in case that all are no zoners */
        if (genndx > 0)
            FLUID_LOG (FLUID_WARN,
                    _("No instrument generators and terminal index not 0"));
        if (modndx > 0)
            FLUID_LOG (FLUID_WARN,
                    _("No instrument modulators and terminal index not 0"));
        return (OK);
    }

    if (genndx < pgenndx)
        return (mem_gerr (ErrCorr, _("Instrument generator indices not monotonic")));
    if (modndx < pmodndx)
        return (mem_gerr (ErrCorr, _("Instrument modulator indices not monotonic")));
    i = genndx - pgenndx;
    while (i--)
        pz->gen = fluid_list_prepend (pz->gen, NULL);
    i = modndx - pmodndx;
    while (i--)
        pz->mod = fluid_list_prepend (pz->mod, NULL);

    return (OK);
}

/* instrument modulator loader */
static int
mem_load_imod (int size, SFData * sf, const void ** data)
{
    fluid_list_t *p, *p2, *p3;
    SFMod *m;

    p = sf->inst;
    while (p)
    {				/* traverse through all inst */
        p2 = ((SFInst *) (p->data))->zone;
        while (p2)
        {			/* traverse this inst's zones */
            p3 = ((SFZone *) (p2->data))->mod;
            while (p3)
            {			/* load zone's modulators */
                if ((size -= SFMODSIZE) < 0)
                    return (mem_gerr (ErrCorr,
                            _("Instrument modulator chunk size mismatch")));
                m = FLUID_NEW (SFMod);
                p3->data = m;
                READW (m->src, data);
                READW (m->dest, data);
                READW (m->amount, data);
                READW (m->amtsrc, data);
                READW (m->trans, data);
                p3 = fluid_list_next (p3);
            }
            p2 = fluid_list_next (p2);
        }
        p = fluid_list_next (p);
    }

    /*
       If there isn't even a terminal record
       Hmmm, the specs say there should be one, but..
     */
    if (size == 0)
        return (OK);

    size -= SFMODSIZE;
    if (size != 0)
        return (mem_gerr (ErrCorr, _("Instrument modulator chunk size mismatch")));
    FSKIP (SFMODSIZE, data);	/* terminal mod */

    return (OK);
}

/* load instrument generators (see mem_load_pgen for loading rules) */
static int
mem_load_igen (int size, SFData * sf, const void ** data)
{
    fluid_list_t *p, *p2, *p3, *dup, **hz = NULL;
    SFZone *z;
    SFGen *g;
    SFGenAmount genval;
    unsigned short genid;
    int level, skip, drop, gzone, discarded;

    p = sf->inst;
    while (p)
    {				/* traverse through all instruments */
        gzone = FALSE;
        discarded = FALSE;
        p2 = ((SFInst *) (p->data))->zone;
        if (p2)
            hz = &p2;
        while (p2)
        {			/* traverse this instrument's zones */
            level = 0;
            z = (SFZone *) (p2->data);
            p3 = z->gen;
            while (p3)
            {			/* load zone's generators */
                dup = NULL;
                skip = FALSE;
                drop = FALSE;
                if ((size -= SFGENSIZE) < 0)
                    return (mem_gerr (ErrCorr, _("IGEN chunk size mismatch")));

                READW (genid, data);

                if (genid == Gen_KeyRange)
                {		/* nothing precedes */
                    if (level == 0)
                    {
                        level = 1;
                        READB (genval.range.lo, data);
                        READB (genval.range.hi, data);
                    }
                    else
                        skip = TRUE;
                }
                else if (genid == Gen_VelRange)
                {		/* only KeyRange precedes */
                    if (level <= 1)
                    {
                        level = 2;
                        READB (genval.range.lo, data);
                        READB (genval.range.hi, data);
                    }
                    else
                        skip = TRUE;
                }
                else if (genid == Gen_SampleId)
                {		/* sample is last gen */
                    level = 3;
                    READW (genval.uword, data);
                    ((SFZone *) (p2->data))->instsamp = (void *) (genval.uword + 1);
                    break;	/* break out of generator loop */
                }
                else
                {
                    level = 2;
                    if (mem_gen_valid (genid))
                    {		/* gen valid? */
                        READW (genval.sword, data);
                        dup = mem_gen_inlist (genid, z->gen);
                    }
                    else
                        skip = TRUE;
                }

                if (!skip)
                {
                    if (!dup)
                    {		/* if gen ! dup alloc new */
                        g = FLUID_NEW (SFGen);
                        p3->data = g;
                        g->id = genid;
                    }
                    else
                    {
                        g = (SFGen *) (dup->data);
                        drop = TRUE;
                    }
                    g->amount = genval;
                }
                else
                {		/* skip this generator */
                    discarded = TRUE;
                    drop = TRUE;
                    FSKIPW (data);
                }

                if (!drop)
                    p3 = fluid_list_next (p3);	/* next gen */
                else
                SLADVREM (z->gen, p3);

            }			/* generator loop */

            if (level == 3)
            SLADVREM (z->gen, p3);	/* zone has sample? */
            else
            {			/* its a global zone */
                if (!gzone)
                {
                    gzone = TRUE;

                    /* if global zone is not 1st zone, relocate */
                    if (*hz != p2)
                    {
                        void* save = p2->data;
                        FLUID_LOG (FLUID_WARN,
                                _("Instrument \"%s\": Global zone is not first zone"),
                                ((SFPreset *) (p->data))->name);
                        SLADVREM (*hz, p2);
                        *hz = fluid_list_prepend (*hz, save);
                        continue;
                    }
                }
                else
                {		/* previous global zone exists, discard */
                    FLUID_LOG (FLUID_WARN,
                            _("Instrument \"%s\": Discarding invalid global zone"),
                            ((SFInst *) (p->data))->name);
                    mem_sfont_zone_delete (sf, hz, (SFZone *) (p2->data));
                }
            }

            while (p3)
            {			/* Kill any zones following a sample */
                discarded = TRUE;
                if ((size -= SFGENSIZE) < 0)
                    return (mem_gerr (ErrCorr,
                            _("Instrument generator chunk size mismatch")));
                FSKIP (SFGENSIZE, data);
                SLADVREM (z->gen, p3);
            }

            p2 = fluid_list_next (p2);	/* next zone */
        }
        if (discarded)
            FLUID_LOG(FLUID_WARN,
                    _("Instrument \"%s\": Some invalid generators were discarded"),
                    ((SFInst *) (p->data))->name);
        p = fluid_list_next (p);
    }

    /* for those non-terminal record cases, grr! */
    if (size == 0)
        return (OK);

    size -= SFGENSIZE;
    if (size != 0)
        return (mem_gerr (ErrCorr, _("IGEN chunk size mismatch")));
    FSKIP (SFGENSIZE, data);	/* terminal gen */

    return (OK);
}

/* sample header loader */
static int
mem_load_shdr (unsigned int size, SFData * sf, const void ** data)
{
    unsigned int i;
    SFSample *p;

    if (size % SFSHDRSIZE || size == 0)	/* size is multiple of SHDR size? */
        return (mem_gerr (ErrCorr, _("Sample header has invalid size")));

    size = size / SFSHDRSIZE - 1;
    if (size == 0)
    {				/* at least one sample + term record? */
        FLUID_LOG (FLUID_WARN, _("File contains no samples"));
        FSKIP (SFSHDRSIZE, data);
        return (OK);
    }

    /* load all sample headers */
    for (i = 0; i < size; i++)
    {
        p = FLUID_NEW (SFSample);
        sf->sample = fluid_list_append (sf->sample, p);
        READSTR (&p->name, data);
        READD (p->start, data);
        READD (p->end, data);	/* - end, loopstart and loopend */
        READD (p->loopstart, data);	/* - will be checked and turned into */
        READD (p->loopend, data);	/* - offsets in mem_fixup_sample() */
        READD (p->samplerate, data);
        READB (p->origpitch, data);
        READB (p->pitchadj, data);
        FSKIPW (data);		/* skip sample link */
        READW (p->sampletype, data);
        p->samfile = 0;
    }

    FSKIP (SFSHDRSIZE, data);	/* skip terminal shdr */

    return (OK);
}

/* "fixup" (inst # -> inst ptr) instrument references in preset list */
static int
mem_fixup_pgen (SFData * sf)
{
    fluid_list_t *p, *p2, *p3;
    SFZone *z;
    int i;

    p = sf->preset;
    while (p)
    {
        p2 = ((SFPreset *) (p->data))->zone;
        while (p2)
        {			/* traverse this preset's zones */
            z = (SFZone *) (p2->data);
            if ((i = (int) (z->instsamp)))
            {			/* load instrument # */
                p3 = fluid_list_nth (sf->inst, i - 1);
                if (!p3)
                    return (mem_gerr (ErrCorr,
                            _("Preset %03d %03d: Invalid instrument reference"),
                            ((SFPreset *) (p->data))->bank,
                            ((SFPreset *) (p->data))->prenum));
                z->instsamp = p3;
            }
            else
                z->instsamp = NULL;
            p2 = fluid_list_next (p2);
        }
        p = fluid_list_next (p);
    }

    return (OK);
}

/* "fixup" (sample # -> sample ptr) sample references in instrument list */
static int
mem_fixup_igen (SFData * sf)
{
    fluid_list_t *p, *p2, *p3;
    SFZone *z;
    int i;

    p = sf->inst;
    while (p)
    {
        p2 = ((SFInst *) (p->data))->zone;
        while (p2)
        {			/* traverse instrument's zones */
            z = (SFZone *) (p2->data);
            if ((i = (int) (z->instsamp)))
            {			/* load sample # */
                p3 = fluid_list_nth (sf->sample, i - 1);
                if (!p3)
                    return (mem_gerr (ErrCorr,
                            _("Instrument \"%s\": Invalid sample reference"),
                            ((SFInst *) (p->data))->name));
                z->instsamp = p3;
            }
            p2 = fluid_list_next (p2);
        }
        p = fluid_list_next (p);
    }

    return (OK);
}

/* convert sample end, loopstart and loopend to offsets and check if valid */
static int
mem_fixup_sample (SFData * sf)
{
    fluid_list_t *p;
    SFSample *sam;

    p = sf->sample;
    while (p)
    {
        sam = (SFSample *) (p->data);

        /* if sample is not a ROM sample and end is over the sample data chunk
           or sam start is greater than 4 less than the end (at least 4 samples) */
        if ((!(sam->sampletype & FLUID_SAMPLETYPE_ROM)
                && sam->end > sdtachunk_size) || sam->start > (sam->end - 4))
        {
            FLUID_LOG (FLUID_WARN, _("Sample '%s' start/end file positions are invalid,"
                    " disabling and will not be saved"), sam->name);

            /* disable sample by setting all sample markers to 0 */
            sam->start = sam->end = sam->loopstart = sam->loopend = 0;

            return (OK);
        }
        else if (sam->loopend > sam->end || sam->loopstart >= sam->loopend
                || sam->loopstart <= sam->start)
        {			/* loop is fowled?? (cluck cluck :) */
            /* can pad loop by 8 samples and ensure at least 4 for loop (2*8+4) */
            if ((sam->end - sam->start) >= 20)
            {
                sam->loopstart = sam->start + 8;
                sam->loopend = sam->end - 8;
            }
            else
            {			/* loop is fowled, sample is tiny (can't pad 8 samples) */
                sam->loopstart = sam->start + 1;
                sam->loopend = sam->end - 1;
            }
        }

        /* convert sample end, loopstart, loopend to offsets from sam->start */
        sam->end -= sam->start + 1;	/* marks last sample, contrary to SF spec. */
        sam->loopstart -= sam->start;
        sam->loopend -= sam->start;

        p = fluid_list_next (p);
    }

    return (OK);
}

/*=================================sfont.c========================
  Smurf SoundFont Editor
  ================================================================*/


/* optimum chunk area sizes (could be more optimum) */
#define PRESET_CHUNK_OPTIMUM_AREA	256
#define INST_CHUNK_OPTIMUM_AREA		256
#define SAMPLE_CHUNK_OPTIMUM_AREA	256
#define ZONE_CHUNK_OPTIMUM_AREA		256
#define FLUID_MOD_CHUNK_OPTIMUM_AREA		256
#define GEN_CHUNK_OPTIMUM_AREA		256

unsigned short mem_badgen[] = { Gen_Unused1, Gen_Unused2, Gen_Unused3, Gen_Unused4,
        Gen_Reserved1, Gen_Reserved2, Gen_Reserved3, 0
};

unsigned short mem_badpgen[] = { Gen_StartAddrOfs, Gen_EndAddrOfs, Gen_StartLoopAddrOfs,
        Gen_EndLoopAddrOfs, Gen_StartAddrCoarseOfs, Gen_EndAddrCoarseOfs,
        Gen_StartLoopAddrCoarseOfs, Gen_Keynum, Gen_Velocity,
        Gen_EndLoopAddrCoarseOfs, Gen_SampleModes, Gen_ExclusiveClass,
        Gen_OverrideRootKey, 0
};

/* close SoundFont file and delete a SoundFont structure */
void
mem_sfont_close (SFData * sf)
{
    fluid_list_t *p, *p2;

//    if (sf->sffd)
//        fclose (sf->sffd);

    if (sf->fname)
        FLUID_FREE (sf->fname);

    p = sf->info;
    while (p)
    {
        FLUID_FREE (p->data);
        p = fluid_list_next (p);
    }
    delete_fluid_list(sf->info);
    sf->info = NULL;

    p = sf->preset;
    while (p)
    {				/* loop over presets */
        p2 = ((SFPreset *) (p->data))->zone;
        while (p2)
        {			/* loop over preset's zones */
            mem_sfont_free_zone (p2->data);
            p2 = fluid_list_next (p2);
        }			/* free preset's zone list */
        delete_fluid_list (((SFPreset *) (p->data))->zone);
        FLUID_FREE (p->data);	/* free preset chunk */
        p = fluid_list_next (p);
    }
    delete_fluid_list (sf->preset);
    sf->preset = NULL;

    p = sf->inst;
    while (p)
    {				/* loop over instruments */
        p2 = ((SFInst *) (p->data))->zone;
        while (p2)
        {			/* loop over inst's zones */
            mem_sfont_free_zone (p2->data);
            p2 = fluid_list_next (p2);
        }			/* free inst's zone list */
        delete_fluid_list (((SFInst *) (p->data))->zone);
        FLUID_FREE (p->data);
        p = fluid_list_next (p);
    }
    delete_fluid_list (sf->inst);
    sf->inst = NULL;

    p = sf->sample;
    while (p)
    {
        FLUID_FREE (p->data);
        p = fluid_list_next (p);
    }
    delete_fluid_list (sf->sample);
    sf->sample = NULL;

    FLUID_FREE (sf);
}

/* free all elements of a zone (Preset or Instrument) */
void
mem_sfont_free_zone (SFZone * zone)
{
    fluid_list_t *p;

    if (!zone)
        return;

    p = zone->gen;
    while (p)
    {				/* Free gen chunks for this zone */
        if (p->data)
            FLUID_FREE (p->data);
        p = fluid_list_next (p);
    }
    delete_fluid_list (zone->gen);	/* free genlist */

    p = zone->mod;
    while (p)
    {				/* Free mod chunks for this zone */
        if (p->data)
            FLUID_FREE (p->data);
        p = fluid_list_next (p);
    }
    delete_fluid_list (zone->mod);	/* free modlist */

    FLUID_FREE (zone);	/* free zone chunk */
}

/* preset sort function, first by bank, then by preset # */
int
mem_sfont_preset_compare_func (void* a, void* b)
{
    int aval, bval;

    aval = (int) (((SFPreset *) a)->bank) << 16 | ((SFPreset *) a)->prenum;
    bval = (int) (((SFPreset *) b)->bank) << 16 | ((SFPreset *) b)->prenum;

    return (aval - bval);
}

/* delete zone from zone list */
void
mem_sfont_zone_delete (SFData * sf, fluid_list_t ** zlist, SFZone * zone)
{
    *zlist = fluid_list_remove (*zlist, (void*) zone);
    mem_sfont_free_zone (zone);
}

/* Find generator in gen list */
fluid_list_t *
mem_gen_inlist (int gen, fluid_list_t * genlist)
{				/* is generator in gen list? */
    fluid_list_t *p;

    p = genlist;
    while (p)
    {
        if (p->data == NULL)
            return (NULL);
        if (gen == ((SFGen *) p->data)->id)
            break;
        p = fluid_list_next (p);
    }
    return (p);
}

/* check validity of instrument generator */
int
mem_gen_valid (int gen)
{				/* is generator id valid? */
    int i = 0;

    if (gen > Gen_MaxValid)
        return (FALSE);
    while (mem_badgen[i] && mem_badgen[i] != gen)
        i++;
    return (mem_badgen[i] == 0);
}

/* check validity of preset generator */
int
mem_gen_validp (int gen)
{				/* is preset generator valid? */
    int i = 0;

    if (!mem_gen_valid (gen))
        return (FALSE);
    while (mem_badpgen[i] && mem_badpgen[i] != (unsigned short) gen)
        i++;
    return (mem_badpgen[i] == 0);
}

/*================================util.c===========================*/

/* Logging function, returns FAIL to use as a return value in calling funcs */
int
mem_gerr (int ev, char * fmt, ...)
{
    va_list args;

    va_start (args, fmt);
    vprintf(fmt, args);
    va_end (args);

    printf("\n");

    return (FAIL);
}

int
mem_safe_fread (void *buf, int count, uint8_t ** data)
{
    //if (fread (buf, count, 1, data) != 1)
    memcpy(buf, *data, count);
//    {				/* size_t = count, nmemb = 1 */
//        if (feof (fd))
//            mem_gerr (ErrEof, _("EOF while attemping to read %d bytes"), count);
//        else
//            FLUID_LOG (FLUID_ERR, _("File read failed"));
//        return (FAIL);
//    }
    *data += count;

    return (OK);
}

int
mem_safe_fseek (uint8_t ** data, long ofs, int whence) {
    if (whence == SEEK_CUR) {
        *data += ofs;
    }
//    if (fseek (data, ofs, whence) == -1) {
//        FLUID_LOG (FLUID_ERR, _("File seek failed with offset = %ld and whence = %d"), ofs, whence);
//        return (FAIL);
//    }
    return (OK);
}

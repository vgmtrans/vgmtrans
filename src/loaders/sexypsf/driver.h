#include "types.h"

typedef struct __PSFTAG
{
 char *key;
 char *value;
 struct __PSFTAG *next;
} PSFTAG;

typedef struct {
        u32 length;
        u32 stop;
        u32 fade;
        char *title,*artist,*game,*year,*genre,*psfby,*comment,*copyright;
        PSFTAG *tags;
} PSFINFO;

int sexy_seek(u32 t);
void sexy_stop(void);
void sexy_execute(u32 numCycles);
s8* GetPSXMainMemBuf();

PSFINFO *sexy_load(const wchar_t *path);
PSFINFO *sexy_getpsfinfo(const wchar_t *path);
void sexy_freepsfinfo(PSFINFO *info);

//void sexyd_update(unsigned char*,long);

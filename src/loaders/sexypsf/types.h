#ifndef _SPSF_TYPES_H__
#define _SPSF_TYPES_H__

//#include <inttypes.h>

#define INLINE inline

typedef char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;
        
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;


/* For gcc only? */
#define PACKSTRUCT	__attribute__ ((packed))

#endif

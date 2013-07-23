#pragma once

#include <stdint.h>

typedef uint32_t U32;
typedef int32_t S32;
typedef uint16_t U16;
typedef int16_t S16;
typedef uint8_t U8;
typedef int8_t S8;

#ifndef _WIN32

typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef unsigned long ULONG;
typedef long LONG;
typedef bool BOOL;
typedef unsigned int UINT;
typedef int INT;
typedef uint16_t USHORT;
typedef int16_t SHORT;
#define FALSE false
#define TRUE true

#endif

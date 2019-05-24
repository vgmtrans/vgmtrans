/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <string>
#include <sstream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <climits>

#include "helper.h"

#define VERSION "1.0.3"

/* Type aliases to save some typing */
using size_t = std::size_t;

using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using sptr = std::intptr_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using uptr = std::uintptr_t;

#define FORWARD_DECLARE_TYPEDEF_STRUCT(type) \
    struct _##type;                          \
    typedef _##type type

std::wstring StringToUpper(std::wstring myString);
std::wstring StringToLower(std::wstring myString);

uint32_t StringToHex(const std::string &str);

std::wstring ConvertToSafeFileName(const std::wstring &str);

std::string wstring2string(std::wstring &wstr);
std::wstring string2wstring(std::string &str);

inline int CountBytesOfVal(uint8_t *buf, uint32_t numBytes, uint8_t val) {
    int count = 0;
    for (uint32_t i = 0; i < numBytes; i++)
        if (buf[i] == val)
            count++;
    return count;
}

struct SizeOffsetPair {
    uint32_t size;
    uint32_t offset;

    SizeOffsetPair() : size(0), offset(0) {}

    SizeOffsetPair(uint32_t offset_, uint32_t size_) : size(size_), offset(offset_) {}
};

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile);

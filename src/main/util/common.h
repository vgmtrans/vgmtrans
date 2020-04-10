/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <string>
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

std::string removeExtFromPath(const std::string &s);

std::string StringToUpper(std::string myString);
std::string StringToLower(std::string myString);

std::string ConvertToSafeFileName(const std::string &str);

struct SizeOffsetPair {
    std::uint32_t size;
    std::uint32_t offset;

    SizeOffsetPair() : size(0), offset(0) {}

    SizeOffsetPair(std::uint32_t offset_, std::uint32_t size_) : size(size_), offset(offset_) {}
};

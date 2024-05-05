/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <string>
#include <cassert>
#include <cstdint>
#include <variant>

#ifdef __FILE_NAME__
#define THIS_FILE_NAME __FILE_NAME__
#else
#define THIS_FILE_NAME ""
#endif


#define KILOBYTE 1024
#define MEGABYTE (KILOBYTE*1024)
#define GIGABYTE (MEGABYTE*1024)

#define F_EPSILON 0.00001

#define FORWARD_DECLARE_TYPEDEF_STRUCT(type) \
    struct _##type;    \
    typedef _##type type

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

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

using VGMFileVariant = std::variant<VGMSeq*, VGMInstrSet*, VGMSampColl*, VGMMiscFile*>;

std::string removeExtFromPath(const std::string& s);

std::string StringToUpper(std::string myString);
std::string StringToLower(std::string myString);

std::string ConvertToSafeFileName(const std::string &str);

template<typename T, std::size_t N>
constexpr std::size_t countof(T const (&)[N]) noexcept {
  return N;
}

inline uint16_t swap_bytes16(uint16_t val) {
  return (val << 8) | (val >> 8);
}

inline uint32_t swap_bytes32(uint32_t val) {
  return ((val << 24) | ((val & 0x0000FF00) << 8) | ((val & 0x00FF0000) >> 8) | (val >> 24));
}

struct SizeOffsetPair {
  std::uint32_t size;
  std::uint32_t offset;

  SizeOffsetPair() : size(0), offset(0) {}

  SizeOffsetPair(std::uint32_t offset_, std::uint32_t size_) : size(size_), offset(offset_) {}
};

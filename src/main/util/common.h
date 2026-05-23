/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <cassert>
#include <cstdint>
#include <variant>
#include <filesystem>

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

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

using VGMFileVariant = std::variant<VGMSeq*, VGMInstrSet*, VGMSampColl*, VGMMiscFile*>;

std::string toUpper(const std::string& input);
std::string toLower(const std::string& input);

std::filesystem::path makeSafeFileName(std::string_view s);
std::string pathToUtf8String(const std::filesystem::path& path);

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

enum class Endianness { Little, Big };
enum class Signedness { Signed, Unsigned };

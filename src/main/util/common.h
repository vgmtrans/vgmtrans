/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <string>
#include <string_view>
#include <cassert>
#include <variant>
#include <filesystem>

#include "types.h"

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

inline u16 swap_bytes16(u16 val) {
  return (val << 8) | (val >> 8);
}

inline u32 swap_bytes32(u32 val) {
  return ((val << 24) | ((val & 0x0000FF00) << 8) | ((val & 0x00FF0000) >> 8) | (val >> 24));
}

struct SizeOffsetPair {
  u32 size;
  u32 offset;

  SizeOffsetPair() : size(0), offset(0) {}

  SizeOffsetPair(u32 offset_, u32 size_) : size(size_), offset(offset_) {}
};

enum class Endianness { Little, Big };
enum class Signedness { Signed, Unsigned };

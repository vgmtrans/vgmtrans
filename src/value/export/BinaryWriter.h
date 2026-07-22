/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

void writeAscii(std::vector<u8>& bytes, std::string_view text);
void writeU8(std::vector<u8>& bytes, u8 value);
void writeLe16(std::vector<u8>& bytes, u16 value);
void writeLeS16(std::vector<u8>& bytes, s16 value);
void writeLe32(std::vector<u8>& bytes, u32 value);
void writeLeS32(std::vector<u8>& bytes, s32 value);
void writeBe16(std::vector<u8>& bytes, u16 value);
void writeBe32(std::vector<u8>& bytes, u32 value);
void writeFixedString(std::vector<u8>& bytes, std::string_view text, size_t width);

struct RiffChunk {
  std::string id;
  u32 size = 0;
  std::vector<u8> payload;
};

[[nodiscard]] RiffChunk makeChunk(std::string id, std::vector<u8> payload);
[[nodiscard]] RiffChunk makeListChunk(std::string type, std::vector<RiffChunk> children);
void appendChunk(std::vector<u8>& bytes, const RiffChunk& chunk);
[[nodiscard]] u32 chunkStorageSize(const RiffChunk& chunk);
[[nodiscard]] std::vector<u8> makeRiff(std::string type, std::vector<RiffChunk> children);

}  // namespace vgmtrans::core

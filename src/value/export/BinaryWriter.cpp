/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/BinaryWriter.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

void writeAscii(std::vector<u8>& bytes, std::string_view text) {
  bytes.insert(bytes.end(), text.begin(), text.end());
}

void writeU8(std::vector<u8>& bytes, u8 value) {
  bytes.push_back(value);
}

void writeLe16(std::vector<u8>& bytes, u16 value) {
  bytes.push_back(static_cast<u8>(value));
  bytes.push_back(static_cast<u8>(value >> 8));
}

void writeLeS16(std::vector<u8>& bytes, s16 value) {
  writeLe16(bytes, static_cast<u16>(value));
}

void writeLe32(std::vector<u8>& bytes, u32 value) {
  bytes.push_back(static_cast<u8>(value));
  bytes.push_back(static_cast<u8>(value >> 8));
  bytes.push_back(static_cast<u8>(value >> 16));
  bytes.push_back(static_cast<u8>(value >> 24));
}

void writeLeS32(std::vector<u8>& bytes, s32 value) {
  writeLe32(bytes, static_cast<u32>(value));
}

void writeBe16(std::vector<u8>& bytes, u16 value) {
  bytes.push_back(static_cast<u8>(value >> 8));
  bytes.push_back(static_cast<u8>(value));
}

void writeBe32(std::vector<u8>& bytes, u32 value) {
  bytes.push_back(static_cast<u8>(value >> 24));
  bytes.push_back(static_cast<u8>(value >> 16));
  bytes.push_back(static_cast<u8>(value >> 8));
  bytes.push_back(static_cast<u8>(value));
}

void writeFixedString(std::vector<u8>& bytes, std::string_view text, size_t width) {
  const auto copied = std::min(text.size(), width);
  bytes.insert(bytes.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(copied));
  bytes.insert(bytes.end(), width - copied, 0);
}

RiffChunk makeChunk(std::string id, std::vector<u8> payload) {
  if (payload.size() > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("RIFF chunk is too large");
  }
  const u32 size = static_cast<u32>(payload.size());
  if ((payload.size() & 1) != 0) {
    payload.push_back(0);
  }
  return RiffChunk{.id = std::move(id), .size = size, .payload = std::move(payload)};
}

void appendChunk(std::vector<u8>& bytes, const RiffChunk& chunk) {
  writeAscii(bytes, chunk.id);
  writeLe32(bytes, chunk.size);
  bytes.insert(bytes.end(), chunk.payload.begin(), chunk.payload.end());
}

u32 chunkStorageSize(const RiffChunk& chunk) {
  if (chunk.payload.size() > std::numeric_limits<u32>::max() - 8) {
    throw std::overflow_error("RIFF chunk is too large");
  }
  return static_cast<u32>(8 + chunk.payload.size());
}

RiffChunk makeListChunk(std::string type, std::vector<RiffChunk> children) {
  std::vector<u8> payload;
  writeAscii(payload, type);
  for (const auto& child : children) {
    appendChunk(payload, child);
  }
  return makeChunk("LIST", std::move(payload));
}

std::vector<u8> makeRiff(std::string type, std::vector<RiffChunk> children) {
  std::vector<u8> payload;
  writeAscii(payload, type);
  for (const auto& child : children) {
    appendChunk(payload, child);
  }
  if (payload.size() > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("RIFF payload is too large");
  }

  std::vector<u8> bytes;
  writeAscii(bytes, "RIFF");
  writeLe32(bytes, static_cast<u32>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

}  // namespace vgmtrans::core

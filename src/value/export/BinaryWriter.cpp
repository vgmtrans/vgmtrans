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

void appendChunk(std::vector<u8>& bytes, const RiffChunk& chunk) {
  if (chunk.payload.size() > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("RIFF chunk is too large");
  }
  writeAscii(bytes, chunk.id);
  writeLe32(bytes, static_cast<u32>(chunk.payload.size()));
  bytes.insert(bytes.end(), chunk.payload.begin(), chunk.payload.end());
  if ((chunk.payload.size() & 1) != 0) {
    bytes.push_back(0);
  }
}

u32 chunkStorageSize(const RiffChunk& chunk) {
  const size_t padding = chunk.payload.size() & 1;
  if (chunk.payload.size() > std::numeric_limits<u32>::max() - 8 - padding) {
    throw std::overflow_error("RIFF chunk is too large");
  }
  return static_cast<u32>(8 + chunk.payload.size() + padding);
}

RiffChunk makeListChunk(std::string_view type, std::span<const RiffChunk> children) {
  std::vector<u8> payload;
  writeAscii(payload, type);
  for (const auto& child : children) {
    appendChunk(payload, child);
  }
  return RiffChunk{"LIST", std::move(payload)};
}

std::vector<u8> makeRiff(std::string_view type, std::span<const RiffChunk> children) {
  std::vector<u8> bytes;
  writeAscii(bytes, "RIFF");
  writeLe32(bytes, 0);
  writeAscii(bytes, type);
  for (const auto& child : children) {
    appendChunk(bytes, child);
  }
  const size_t payloadSize = bytes.size() - 8;
  if (payloadSize > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("RIFF payload is too large");
  }
  // Fill the RIFF size after writing its children directly into the final buffer.
  for (u32 byte = 0; byte < 4; ++byte) {
    bytes[4 + byte] = static_cast<u8>(payloadSize >> (8 * byte));
  }
  return bytes;
}

}  // namespace vgmtrans::core

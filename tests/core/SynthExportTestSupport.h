/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

// Inspect the serialized bytes independently of the production writers.
u32 readLe32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u32>(bytes.at(offset)) | (static_cast<u32>(bytes.at(offset + 1)) << 8) |
         (static_cast<u32>(bytes.at(offset + 2)) << 16) | (static_cast<u32>(bytes.at(offset + 3)) << 24);
}

u16 readLe16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u16>(bytes.at(offset)) | (static_cast<u16>(bytes.at(offset + 1)) << 8);
}

s16 readLeS16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s16>(readLe16(bytes, offset));
}

s32 readLeS32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s32>(readLe32(bytes, offset));
}

bool containsAscii(const std::vector<u8>& bytes, std::string_view text) {
  return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

size_t asciiOffset(const std::vector<u8>& bytes, std::string_view text) {
  const auto found = std::search(bytes.begin(), bytes.end(), text.begin(), text.end());
  if (found == bytes.end()) {
    throw std::runtime_error("expected ASCII marker was not found");
  }
  return static_cast<size_t>(std::distance(bytes.begin(), found));
}

u32 chunkSize(const std::vector<u8>& bytes, std::string_view chunkId) {
  return readLe32(bytes, asciiOffset(bytes, chunkId) + 4);
}

bool soundFontInfoChunksHaveEvenDeclaredSizes(const std::vector<u8>& bytes) {
  const auto infoTypeOffset = asciiOffset(bytes, "INFO");
  if (infoTypeOffset < 8) {
    return false;
  }

  const std::string_view listId(reinterpret_cast<const char*>(bytes.data() + infoTypeOffset - 8), 4);
  if (listId != "LIST") {
    return false;
  }

  const size_t infoListSize = readLe32(bytes, infoTypeOffset - 4);
  const size_t infoEnd = infoTypeOffset + infoListSize;
  if (infoEnd > bytes.size()) {
    return false;
  }
  size_t offset = infoTypeOffset + 4;
  while (offset + 8 <= infoEnd) {
    const u32 size = readLe32(bytes, offset + 4);
    if ((size & 1u) != 0) {
      return false;
    }
    offset += 8 + size + (size & 1u);
  }
  return offset == infoEnd;
}

bool soundFontGeneratorContains(const std::vector<u8>& bytes, std::string_view chunkId, u16 generator,
                                s16 expectedAmount) {
  const auto chunkOffset = asciiOffset(bytes, chunkId);
  const auto size = chunkSize(bytes, chunkId);
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 4 <= payloadOffset + size; offset += 4) {
    if (readLe16(bytes, offset) == generator && readLeS16(bytes, offset + 2) == expectedAmount) {
      return true;
    }
  }
  return false;
}

bool soundFontBagAt(const std::vector<u8>& bytes, std::string_view chunkId, size_t index, u16 genIndex, u16 modIndex) {
  const auto chunkOffset = asciiOffset(bytes, chunkId);
  const auto size = chunkSize(bytes, chunkId);
  const auto offset = chunkOffset + 8 + (index * 4);
  if (offset + 4 > chunkOffset + 8 + size) {
    return false;
  }

  return readLe16(bytes, offset) == genIndex && readLe16(bytes, offset + 2) == modIndex;
}

bool soundFontImodContains(const std::vector<u8>& bytes, u16 source, u16 destination, s16 amount) {
  const auto chunkOffset = asciiOffset(bytes, "imod");
  const auto size = chunkSize(bytes, "imod");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 10 <= payloadOffset + size; offset += 10) {
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == destination &&
        readLeS16(bytes, offset + 4) == amount) {
      return true;
    }
  }
  return false;
}

template <typename Match>
bool dlsArt2Contains(const std::vector<u8>& bytes, Match matches) {
  auto searchFrom = bytes.begin();
  constexpr std::string_view chunkId = "art2";
  while (searchFrom != bytes.end()) {
    const auto found = std::search(searchFrom, bytes.end(), chunkId.begin(), chunkId.end());
    if (found == bytes.end()) {
      return false;
    }
    const size_t chunkOffset = static_cast<size_t>(std::distance(bytes.begin(), found));
    if (chunkOffset + 16 <= bytes.size()) {
      const size_t chunkEnd = std::min(bytes.size(), chunkOffset + 8 + readLe32(bytes, chunkOffset + 4));
      const size_t connections = readLe32(bytes, chunkOffset + 12);
      for (size_t offset = chunkOffset + 16, i = 0; i < connections && offset + 12 <= chunkEnd; ++i, offset += 12) {
        if (matches(offset)) {
          return true;
        }
      }
    }
    searchFrom = found + static_cast<std::ptrdiff_t>(chunkId.size());
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 destination, s32 expectedScale) {
  return dlsArt2Contains(bytes, [&](size_t offset) {
    return readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale;
  });
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 source, u16 destination, s32 expectedScale) {
  return dlsArt2Contains(bytes, [&](size_t offset) {
    return readLe16(bytes, offset) == source && readLe16(bytes, offset + 4) == destination &&
           readLeS32(bytes, offset + 8) == expectedScale;
  });
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 source, u16 control, u16 destination,
                               s32 expectedScale) {
  return dlsArt2Contains(bytes, [&](size_t offset) {
    return readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == control &&
           readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale;
  });
}

}  // namespace

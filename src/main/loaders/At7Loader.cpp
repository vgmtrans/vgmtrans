/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "At7Loader.h"

#include "LoaderManager.h"
#include "LogManager.h"

#include <array>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr u32 kAt7pMagic = 0x41543750;  // AT7P
constexpr u32 kAt7xMagic = 0x41543758;  // AT7X
constexpr u32 kAt7eMagic = 0x41543745;  // AT7E
constexpr size_t kHeaderSize = 6;
constexpr size_t kHistorySize = 0x1000;
constexpr size_t kMaxDecodedSize = 512 * 1024 * 1024;
}  // namespace

namespace vgmtrans::loaders {
LoaderRegistration<At7Loader> _at7("AT7");
}

void At7Loader::apply(const RawFile* file) {
  if (file->size() < kHeaderSize || file->readWordBE(0) != kAt7pMagic) {
    return;
  }

  std::vector<u8> decoded;
  size_t offset = 0;
  bool reachedEnd = false;
  while (offset + sizeof(u32) <= file->size()) {
    const u32 magic = file->readWordBE(offset);
    if (magic == kAt7eMagic) {
      reachedEnd = true;
      break;
    }
    if ((magic != kAt7pMagic && magic != kAt7xMagic) || offset + kHeaderSize > file->size()) {
      return;
    }

    const size_t storedLength = file->readShort(offset + 4);
    const size_t segmentLength = magic == kAt7pMagic ? storedLength : storedLength + kHeaderSize;
    if (segmentLength < kHeaderSize || segmentLength > file->size() - offset) {
      return;
    }

    const size_t dataLength = segmentLength - kHeaderSize;
    if (magic == kAt7xMagic) {
      if (dataLength > kMaxDecodedSize - decoded.size()) {
        return;
      }
      const auto* data = reinterpret_cast<const u8*>(file->data() + offset + kHeaderSize);
      decoded.insert(decoded.end(), data, data + dataLength);
    } else if (!decompressSegment(reinterpret_cast<const u8*>(file->data() + offset + kHeaderSize), dataLength,
                                  decoded)) {
      return;
    }
    offset += segmentLength;
  }

  if (!reachedEnd || decoded.empty() || decoded.size() > std::numeric_limits<u32>::max()) {
    return;
  }

  const auto path = std::filesystem::path(file->name());
  // [Pokemon Fushigi no Dungeon: Ikuzo! Arashi no Boukendan]: AT7P contains the complete DSE asset payload.
  // [Pokemon Fushigi no Dungeon: Susume! Honou no Boukendan]: AT7P contains the complete DSE asset payload.
  // [Pokemon Fushigi no Dungeon: Mezase! Hikari no Boukendan]: AT7P contains the complete DSE asset payload.
  // The virtual extension restricts the decoded blob to scanners that recognize DSE data.
  enqueue(std::make_unique<VirtFile>(decoded.data(), static_cast<u32>(decoded.size()), path.stem().string() + ".dse",
                                     file->path(), file->tag));
}

bool At7Loader::decompressSegment(const u8* input, size_t inputSize, std::vector<u8>& output) {
  if (output.size() > kMaxDecodedSize) {
    return false;
  }

  std::array<u8, kHistorySize> history{};
  size_t historyOffset = 0;
  size_t inputOffset = 0;
  while (inputOffset < inputSize) {
    const u8 flags = input[inputOffset++];
    for (u8 mask = 0x80; mask != 0 && inputOffset < inputSize; mask >>= 1) {
      if (output.size() == kMaxDecodedSize) {
        return false;
      }

      if ((flags & mask) != 0) {
        const u8 value = input[inputOffset++];
        output.push_back(value);
        history[historyOffset] = value;
        historyOffset = (historyOffset + 1) & (kHistorySize - 1);
        continue;
      }

      if (inputSize - inputOffset < sizeof(u16)) {
        return false;
      }
      const u16 code = (static_cast<u16>(input[inputOffset]) << 8) | input[inputOffset + 1];
      inputOffset += sizeof(u16);
      const size_t length = (code >> 12) + 3;
      const size_t distance = kHistorySize - (code & (kHistorySize - 1));
      size_t sourceOffset = (historyOffset + kHistorySize - distance) & (kHistorySize - 1);
      if (length > kMaxDecodedSize - output.size()) {
        return false;
      }

      for (size_t i = 0; i < length; ++i) {
        const u8 value = history[sourceOffset];
        sourceOffset = (sourceOffset + 1) & (kHistorySize - 1);
        output.push_back(value);
        history[historyOffset] = value;
        historyOffset = (historyOffset + 1) & (kHistorySize - 1);
      }
    }
  }
  return true;
}

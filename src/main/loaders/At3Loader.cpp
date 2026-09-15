/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "At3Loader.h"

#include "LoaderManager.h"
#include "LogManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr u32 kAt3pMagic = 0x41543350;  // AT3P
constexpr size_t kDirectoryHeaderSize = 0x20;
constexpr size_t kDirectoryEntrySize = 0x20;
constexpr size_t kNameSize = 0x1c;
constexpr size_t kChunkHeaderSize = 0x10;
constexpr size_t kMemberDataOffset = 0x2000;
constexpr size_t kMaxDecodedSize = 64 * 1024 * 1024;

std::string lowerAscii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}
}  // namespace

namespace vgmtrans::loaders {
LoaderRegistration<At3Loader> _at3("AT3P");
}

void At3Loader::apply(const RawFile* file) {
  if (lowerAscii(file->name()) != "body.dat" || file->size() <= kMemberDataOffset + kChunkHeaderSize ||
      file->readWordBE(kMemberDataOffset) != kAt3pMagic) {
    return;
  }

  const std::filesystem::path directoryPath = file->path().parent_path() / "direntry.dat";
  std::unique_ptr<DiskFile> directory;
  try {
    directory = std::make_unique<DiskFile>(directoryPath);
  } catch (...) {
    return;
  }

  if (directory->size() < kDirectoryHeaderSize) {
    return;
  }
  const u32 memberCount = directory->readWordBE(0);
  if (memberCount == 0 || memberCount > (directory->size() - kDirectoryHeaderSize) / kDirectoryEntrySize) {
    return;
  }

  struct Member {
    std::string name;
    size_t offset;
  };
  std::vector<Member> members;
  for (u32 index = 0; index < memberCount; ++index) {
    const size_t entryOffset = kDirectoryHeaderSize + static_cast<size_t>(index) * kDirectoryEntrySize;
    const char* nameStart = directory->data() + entryOffset;
    const auto* nameEnd = static_cast<const char*>(std::memchr(nameStart, 0, kNameSize));
    if (nameEnd == nullptr || nameEnd == nameStart || directory->readByte(entryOffset + 0x1e) != 0x20 ||
        directory->readByte(entryOffset + 0x1f) != 0) {
      return;
    }

    std::string name(nameStart, nameEnd);
    if (!lowerAscii(name).ends_with(".se")) {
      continue;
    }
    // [Shiren the Wanderer]: Each directory locator selects a 64 KiB block whose member stream begins at 0x2000.
    const size_t block = directory->readShortBE(entryOffset + 0x1c);
    if (block > (std::numeric_limits<size_t>::max() - kMemberDataOffset) >> 16) {
      return;
    }
    const size_t memberOffset = (block << 16) + kMemberDataOffset;
    if (memberOffset > file->size() || file->size() - memberOffset < kChunkHeaderSize ||
        file->readWordBE(memberOffset) != kAt3pMagic) {
      return;
    }
    members.push_back({std::move(name), memberOffset});
  }

  // [Shiren the Wanderer]: The body archive's .se members pair an SWDB sample bank with an SEDB effect set.
  // Decoding only those members avoids materializing hundreds of megabytes of unrelated graphics and scripts.
  for (const auto& member : members) {
    std::vector<u8> decoded;
    if (!decodeMember(file, member.offset, decoded)) {
      L_WARN("Failed to decode AT3P member '{}' in '{}'", member.name, file->name());
      continue;
    }
    if (decoded.empty() || decoded.size() > std::numeric_limits<u32>::max()) {
      continue;
    }
    enqueue(std::make_unique<VirtFile>(decoded.data(), static_cast<u32>(decoded.size()), member.name + ".dse",
                                       file->path(), file->tag));
  }
}

bool At3Loader::decodeMember(const RawFile* body, size_t offset, std::vector<u8>& output) {
  output.clear();
  while (offset <= body->size() && body->size() - offset >= 5 && body->readWordBE(offset) == kAt3pMagic) {
    const u8 mode = body->readByte(offset + 4);
    if (mode == 'E') {
      return true;
    }
    if (body->size() - offset < kChunkHeaderSize || (mode != 'N' && mode != 'X')) {
      return false;
    }

    const size_t storedLength = body->readShort(offset + 5);
    // [Shiren the Wanderer]: AT3PN stores its payload length after a padded 16-byte header, while AT3PX stores the
    // total chunk length including that header. The retail decoder initializes the two input counters accordingly.
    if (mode == 'N') {
      if (storedLength > kMaxDecodedSize - output.size() ||
          storedLength > body->size() - offset - kChunkHeaderSize) {
        return false;
      }
      const auto* data = reinterpret_cast<const u8*>(body->data() + offset + kChunkHeaderSize);
      output.insert(output.end(), data, data + storedLength);
      offset += kChunkHeaderSize + storedLength;
      continue;
    }

    if (storedLength < kChunkHeaderSize || storedLength > body->size() - offset) {
      return false;
    }
    const auto* controlFlags = reinterpret_cast<const u8*>(body->data() + offset + 7);
    const auto* input = reinterpret_cast<const u8*>(body->data() + offset + kChunkHeaderSize);
    if (!decompressSegment(input, storedLength - kChunkHeaderSize, controlFlags, output)) {
      return false;
    }
    offset += storedLength;
  }
  return false;
}

bool At3Loader::decompressSegment(const u8* input, size_t inputSize, const u8* controlFlags,
                                  std::vector<u8>& output) {
  size_t inputOffset = 0;
  while (inputOffset < inputSize) {
    const u8 command = input[inputOffset++];
    for (u8 mask = 0x80; mask != 0 && inputOffset < inputSize; mask >>= 1) {
      if ((command & mask) != 0) {
        if (output.size() == kMaxDecodedSize) {
          return false;
        }
        output.push_back(input[inputOffset++]);
        continue;
      }

      const u8 token = input[inputOffset++];
      const u8 high = token >> 4;
      const u8 low = token & 0x0f;
      const u8* found = std::find(controlFlags, controlFlags + 9, high);
      if (found == controlFlags + 9) {
        if (inputOffset == inputSize) {
          return false;
        }
        const size_t windowOffset = (static_cast<size_t>(low) << 8) | input[inputOffset++];
        const size_t distance = 0x1000 - windowOffset;
        const size_t length = static_cast<size_t>(high) + 3;
        if (distance > output.size() || length > kMaxDecodedSize - output.size()) {
          return false;
        }
        for (size_t i = 0; i < length; ++i) {
          output.push_back(output[output.size() - distance]);
        }
        continue;
      }

      const size_t controlIndex = static_cast<size_t>(found - controlFlags);
      std::array<u8, 4> nibbles{};
      if (controlIndex == 0) {
        nibbles.fill(low);
      } else {
        u8 value = low;
        if (controlIndex == 1) {
          value = (value + 1) & 0x0f;
        } else if (controlIndex == 5) {
          value = (value - 1) & 0x0f;
        }
        nibbles.fill(value);
        const size_t adjustedIndex = controlIndex <= 4 ? controlIndex - 1 : controlIndex - 5;
        const int adjustment = controlIndex <= 4 ? -1 : 1;
        nibbles[adjustedIndex] = static_cast<u8>((nibbles[adjustedIndex] + adjustment) & 0x0f);
      }
      if (output.size() > kMaxDecodedSize - 2) {
        return false;
      }
      output.push_back(static_cast<u8>((nibbles[0] << 4) | nibbles[1]));
      output.push_back(static_cast<u8>((nibbles[2] << 4) | nibbles[3]));
    }
  }
  return true;
}

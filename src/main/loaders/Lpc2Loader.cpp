/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Lpc2Loader.h"

#include "LoaderManager.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace {
constexpr u32 kLpc2Magic = 0x4c504332;  // LPC2
constexpr size_t kLpc2HeaderSize = 0x20;
constexpr size_t kLpc2EntrySize = 12;
constexpr size_t kMaxDecodedSize = 512 * 1024 * 1024;
constexpr size_t kMaxMemberNameSize = 256;

u32 readWord(const u8* data) {
  return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) | (static_cast<u32>(data[2]) << 16) |
         (static_cast<u32>(data[3]) << 24);
}

u32 readWordBE(const u8* data) {
  return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) | (static_cast<u32>(data[2]) << 8) |
         static_cast<u32>(data[3]);
}

int dseMemberPriority(const u8* data, size_t size) {
  if (size < 4) {
    return 4;
  }

  const u8 c0 = data[0] | 0x20;
  const u8 c1 = data[1] | 0x20;
  const u8 c2 = data[2] | 0x20;
  const u8 c3 = data[3] | 0x20;
  if (c0 != 's' || (c3 != 'l' && c3 != 'b')) {
    return 4;
  }
  if (c1 == 'w' && c2 == 'd') {
    if (size < 0x44) {
      return 1;
    }
    const u32 pcmdLength = c3 == 'b' ? readWordBE(data + 0x40) : readWord(data + 0x40);
    // [Sands of Destruction]: A zero PCMD length identifies an empty bank rather than an embedded sample bank.
    return pcmdLength == 0 || (pcmdLength & 0xffff0000) == 0xaaaa0000 ? 1 : 0;
  }
  if ((c1 == 'm' || c1 == 'e') && c2 == 'd') {
    return 2;
  }
  return 4;
}
}  // namespace

namespace vgmtrans::loaders {
LoaderRegistration<Lpc2Loader> _lpc2("LZ10/LPC2");
}

void Lpc2Loader::apply(const RawFile* file) {
  if (file->size() >= kLpc2HeaderSize && file->readWordBE(0) == kLpc2Magic) {
    unpackLpc2(file, reinterpret_cast<const u8*>(file->data()), file->size());
    return;
  }

  // [Professor Layton and the Unwound Future]: Each CSND is a Nintendo LZ10 stream containing an LPC2 archive.
  // Checking both the extension and decoded magic distinguishes these archives from other compressed NitroFS data.
  if (file->extension() != "csnd" || file->size() < 5 || file->readByte(0) != 0x10) {
    return;
  }

  std::vector<u8> decoded;
  if (!decompressLz10(file, decoded) || decoded.size() < kLpc2HeaderSize || readWordBE(decoded.data()) != kLpc2Magic) {
    return;
  }
  unpackLpc2(file, decoded.data(), decoded.size());
}

bool Lpc2Loader::decompressLz10(const RawFile* file, std::vector<u8>& output) {
  const size_t expectedSize = static_cast<size_t>(file->readByte(1)) | (static_cast<size_t>(file->readByte(2)) << 8) |
                              (static_cast<size_t>(file->readByte(3)) << 16);
  if (expectedSize == 0 || expectedSize > kMaxDecodedSize) {
    return false;
  }

  output.clear();
  output.reserve(expectedSize);
  size_t inputOffset = 4;
  while (output.size() < expectedSize) {
    if (inputOffset >= file->size()) {
      return false;
    }
    const u8 flags = file->readByte(inputOffset++);
    for (u8 mask = 0x80; mask != 0 && output.size() < expectedSize; mask >>= 1) {
      if ((flags & mask) == 0) {
        if (inputOffset >= file->size()) {
          return false;
        }
        output.push_back(file->readByte(inputOffset++));
        continue;
      }

      if (file->size() - inputOffset < 2) {
        return false;
      }
      const u8 first = file->readByte(inputOffset++);
      const u8 second = file->readByte(inputOffset++);
      const size_t length = (first >> 4) + 3;
      const size_t distance = ((static_cast<size_t>(first & 0x0f) << 8) | second) + 1;
      if (distance > output.size() || length > expectedSize - output.size()) {
        return false;
      }
      for (size_t i = 0; i < length; ++i) {
        output.push_back(output[output.size() - distance]);
      }
    }
  }
  return true;
}

void Lpc2Loader::unpackLpc2(const RawFile* source, const u8* data, size_t size) {
  if (size < kLpc2HeaderSize || readWordBE(data) != kLpc2Magic) {
    return;
  }

  const u32 memberCount = readWord(data + 4);
  const u32 fileDataOffset = readWord(data + 8);
  const u32 archiveEnd = readWord(data + 0x0c);
  const u32 metadataOffset = readWord(data + 0x10);
  const u32 nameBankOffset = readWord(data + 0x14);
  const u32 repeatedFileDataOffset = readWord(data + 0x18);
  const uint64_t metadataEnd =
      static_cast<uint64_t>(metadataOffset) + static_cast<uint64_t>(memberCount) * kLpc2EntrySize;
  if (memberCount == 0 || fileDataOffset != repeatedFileDataOffset || metadataOffset < kLpc2HeaderSize ||
      metadataOffset > nameBankOffset || metadataEnd > nameBankOffset || nameBankOffset > fileDataOffset ||
      fileDataOffset > archiveEnd || archiveEnd > size) {
    return;
  }

  struct Member {
    const u8* data;
    u32 size;
    std::string name;
    int priority;
  };
  std::vector<Member> members;
  members.reserve(memberCount);
  for (u32 i = 0; i < memberCount; ++i) {
    const size_t entryOffset = metadataOffset + static_cast<size_t>(i) * kLpc2EntrySize;
    const u32 relativeNameOffset = readWord(data + entryOffset);
    const u32 relativeDataOffset = readWord(data + entryOffset + 4);
    const u32 memberSize = readWord(data + entryOffset + 8);
    if (relativeNameOffset > fileDataOffset - nameBankOffset || relativeDataOffset > archiveEnd - fileDataOffset ||
        memberSize > archiveEnd - fileDataOffset - relativeDataOffset) {
      return;
    }

    const size_t nameOffset = nameBankOffset + relativeNameOffset;
    const size_t maximumNameSize = std::min(kMaxMemberNameSize, static_cast<size_t>(fileDataOffset - nameOffset));
    const u8* terminator = std::find(data + nameOffset, data + nameOffset + maximumNameSize, 0);
    if (terminator == data + nameOffset + maximumNameSize) {
      return;
    }
    std::string name(reinterpret_cast<const char*>(data + nameOffset),
                     static_cast<size_t>(terminator - (data + nameOffset)));
    if (name.empty()) {
      name = fmt::format("file{}.bin", i);
    }

    const u8* memberData = data + fileDataOffset + relativeDataOffset;
    members.push_back({memberData, memberSize, std::move(name), dseMemberPriority(memberData, memberSize)});
  }

  // [Professor Layton and the Unwound Future]: LPC2 metadata can list an SMDL before its SWDL.
  // Loading waveform banks, program banks, and then sequences completes associations during the first scan.
  std::stable_sort(members.begin(), members.end(),
                   [](const Member& left, const Member& right) { return left.priority < right.priority; });
  for (const auto& member : members) {
    if (member.size == 0) {
      continue;
    }
    enqueue(std::make_unique<VirtFile>(member.data, member.size, member.name, source->path(), source->tag));
  }
}

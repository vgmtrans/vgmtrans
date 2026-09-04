/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "DarcLoader.h"

#include "LoaderManager.h"
#include "LogManager.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace {
constexpr u32 kDarcMagic = 0x44415243;  // DARC
constexpr u32 kDencMagic = 0x44454e43;  // DENC
constexpr u32 kNullCodec = 0x4e554c4c;  // NULL
constexpr u32 kLzssCodec = 0x4c5a5353;  // LZSS

struct DarcMember {
  size_t offset;
  u32 size;
};

std::string decodedExtension(const u8* data, size_t size) {
  if (size < 4) {
    return "bin";
  }

  const u32 magic = (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
                    (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
  switch (magic) {
    case 0x736d646c:  // smdl
    case 0x534d444c:  // SMDL
      return "smd";
    case 0x7377646c:  // swdl
    case 0x5357444c:  // SWDL
      return "swd";
    case 0x7365646c:  // sedl
    case 0x5345444c:  // SEDL
      return "sed";
    case 0x7361646c:  // sadl
    case 0x5341444c:  // SADL
      return "sad";
    case 0x53495230:  // SIR0
      return "sir0";
    default:
      return "bin";
  }
}
}  // namespace

namespace vgmtrans::loaders {
LoaderRegistration<DarcLoader> _darc("DARC/DENC");
}

void DarcLoader::apply(const RawFile* file) {
  if (file->size() < 4) {
    return;
  }

  switch (file->readWordBE(0)) {
    case kDarcMagic:
      unpackDarc(file);
      break;
    case kDencMagic:
      unpackDenc(file);
      break;
    default:
      break;
  }
}

void DarcLoader::unpackDarc(const RawFile* file) {
  if (file->size() < 8) {
    return;
  }

  const u32 memberCount = file->readWord(4);
  if (memberCount == 0 || memberCount > (file->size() - 8) / sizeof(u32)) {
    return;
  }

  std::vector<DarcMember> members;
  members.reserve(memberCount);
  for (u32 i = 0; i < memberCount; ++i) {
    const size_t pointerOffset = 8 + static_cast<size_t>(i) * sizeof(u32);
    const u32 relativeOffset = file->readWord(pointerOffset);
    if (relativeOffset > file->size() - pointerOffset) {
      return;
    }

    const size_t sizeOffset = pointerOffset + relativeOffset;
    if (sizeOffset > file->size() || file->size() - sizeOffset < sizeof(u32)) {
      return;
    }

    const u32 memberSize = file->readWord(sizeOffset);
    const size_t memberOffset = sizeOffset + sizeof(u32);
    if (memberSize > file->size() - memberOffset) {
      return;
    }
    if (memberSize != 0 && (memberSize < sizeof(u32) || file->readWordBE(memberOffset) != kDencMagic)) {
      return;
    }

    members.push_back({memberOffset, memberSize});
  }

  struct DecodedMember {
    size_t index;
    std::string extension;
    std::vector<u8> data;
  };
  std::vector<DecodedMember> decodedMembers;
  for (size_t i = 0; i < members.size(); ++i) {
    const auto [offset, size] = members[i];
    if (size == 0) {
      continue;
    }

    std::vector<u8> decoded;
    if (!decodeDenc(file, offset, size, decoded)) {
      continue;
    }
    const auto extension = decodedExtension(decoded.data(), decoded.size());
    if (extension == "bin") {
      continue;
    }
    decodedMembers.push_back({i, extension, std::move(decoded)});
  }

  // Banks must exist before sequences are matched into collections. DARC's
  // physical order commonly places a sequence before its corresponding SWDL.
  const auto loadPriority = [](const DecodedMember& member) {
    if (member.extension != "swd") {
      return member.extension == "smd" ? 2 : 3;
    }
    if (member.data.size() < 0x44) {
      return 1;
    }
    const bool bigEndian = (member.data[3] | 0x20) == 'b';
    const u32 pcmdLength =
        bigEndian ? (static_cast<u32>(member.data[0x40]) << 24) | (static_cast<u32>(member.data[0x41]) << 16) |
                        (static_cast<u32>(member.data[0x42]) << 8) | member.data[0x43]
                  : static_cast<u32>(member.data[0x40]) | (static_cast<u32>(member.data[0x41]) << 8) |
                        (static_cast<u32>(member.data[0x42]) << 16) | (static_cast<u32>(member.data[0x43]) << 24);
    // Sands of Destruction demonstrates that a zero PCMD length is an empty
    // bank, not an embedded sample bank that should be loaded first.
    return pcmdLength == 0 || (pcmdLength & 0xffff0000) == 0xaaaa0000 ? 1 : 0;
  };
  std::stable_sort(decodedMembers.begin(), decodedMembers.end(),
                   [&loadPriority](const DecodedMember& left, const DecodedMember& right) {
                     return loadPriority(left) < loadPriority(right);
                   });
  for (const auto& member : decodedMembers) {
    enqueue(std::make_unique<VirtFile>(member.data.data(), static_cast<u32>(member.data.size()),
                                       fmt::format("file{:04}.{}", member.index, member.extension), file->path(),
                                       file->tag));
  }
}

void DarcLoader::unpackDenc(const RawFile* file) {
  std::vector<u8> decoded;
  if (!decodeDenc(file, 0, file->size(), decoded)) {
    return;
  }

  const auto extension = decodedExtension(decoded.data(), decoded.size());
  if (extension == "bin") {
    return;
  }

  const auto baseName = std::filesystem::path(file->name()).stem().string();
  enqueue(std::make_unique<VirtFile>(decoded.data(), static_cast<u32>(decoded.size()),
                                     fmt::format("{}.{}", baseName, extension), file->path(), file->tag));
}

bool DarcLoader::decodeDenc(const RawFile* file, size_t offset, size_t size, std::vector<u8>& decoded) {
  constexpr size_t kHeaderSize = 0x10;
  if (size < kHeaderSize || offset > file->size() || size > file->size() - offset ||
      file->readWordBE(offset) != kDencMagic) {
    return false;
  }

  const u32 decodedSize = file->readWord(offset + 4);
  const u32 codec = file->readWordBE(offset + 8);
  const u32 encodedSize = file->readWord(offset + 0x0c);
  if (encodedSize > size - kHeaderSize || decodedSize == 0) {
    return false;
  }

  const auto* encoded = reinterpret_cast<const u8*>(file->data() + offset + kHeaderSize);
  if (codec == kNullCodec) {
    if (encodedSize != decodedSize) {
      return false;
    }
    decoded.assign(encoded, encoded + encodedSize);
  } else if (codec == kLzssCodec) {
    if (!decompressLzss(encoded, encodedSize, decodedSize, decoded)) {
      L_WARN("Failed to decompress LZSS DENC data in '{}'", file->name());
      return false;
    }
  } else {
    L_DEBUG("DENC member '{}' uses unsupported codec {:08x}", file->name(), codec);
    return false;
  }
  return true;
}

bool DarcLoader::decompressLzss(const u8* input, size_t inputSize, size_t expectedSize, std::vector<u8>& output) {
  output.clear();
  output.reserve(expectedSize);

  size_t inputOffset = 0;
  while (inputOffset < inputSize) {
    const u8 token = input[inputOffset++];
    if ((token & 1) == 0) {
      const size_t length = token >> 1;
      if (length > inputSize - inputOffset || length > expectedSize - output.size()) {
        return false;
      }
      output.insert(output.end(), input + inputOffset, input + inputOffset + length);
      inputOffset += length;
      continue;
    }

    if (inputOffset >= inputSize) {
      return false;
    }
    const u16 flags = static_cast<u16>(token) | (static_cast<u16>(input[inputOffset++]) << 8);
    const size_t distance = (flags >> 1) & 0x7ff;
    const size_t length = (flags >> 12) + 2;
    if (distance == 0 || distance > output.size() || length > expectedSize - output.size()) {
      return false;
    }

    for (size_t i = 0; i < length; ++i) {
      output.push_back(output[output.size() - distance]);
    }
  }

  return output.size() == expectedSize;
}

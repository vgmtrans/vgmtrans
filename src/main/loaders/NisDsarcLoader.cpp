/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NisDsarcLoader.h"

#include "LoaderManager.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace {
constexpr size_t kHeaderSize = 0x10;
constexpr size_t kFileEntrySize = 0x30;
constexpr size_t kFileNameSize = 0x28;

struct DsarcMember {
  u32 offset;
  u32 size;
  std::string name;
};
}  // namespace

namespace vgmtrans::loaders {
LoaderRegistration<NisDsarcLoader> _nisDsarc("NIS DSARC");
}

void NisDsarcLoader::apply(const RawFile* file) {
  if (file->size() < kHeaderSize) {
    return;
  }

  const std::string magic(file->data(), 8);
  const bool indexed = magic == "DSARCIDX";
  if (!indexed && magic != "DSARC FL") {
    return;
  }

  const u32 memberCount = file->readWord(8);
  if (memberCount == 0) {
    return;
  }

  size_t entryTableOffset = kHeaderSize;
  if (indexed) {
    if (memberCount > (file->size() - kHeaderSize) / sizeof(u16)) {
      return;
    }
    entryTableOffset += static_cast<size_t>(memberCount) * sizeof(u16);
    entryTableOffset = (entryTableOffset + 3) & ~size_t{3};
  } else if (file->readWord(0x0c) != 1) {
    return;
  }

  if (entryTableOffset > file->size() || memberCount > (file->size() - entryTableOffset) / kFileEntrySize) {
    return;
  }

  std::vector<DsarcMember> members;
  members.reserve(memberCount);
  for (u32 i = 0; i < memberCount; ++i) {
    const size_t entryOffset = entryTableOffset + static_cast<size_t>(i) * kFileEntrySize;
    std::string name = file->readNullTerminatedString(entryOffset, kFileNameSize);
    const u32 size = file->readWord(entryOffset + 0x28);
    const u32 offset = file->readWord(entryOffset + 0x2c);
    if (offset > file->size() || size > file->size() - offset) {
      return;
    }
    if (name.empty()) {
      name = fmt::format("file{:04}.bin", i);
    }
    members.push_back({offset, size, std::move(name)});
  }

  for (const auto& member : members) {
    if (member.size == 0) {
      continue;
    }
    enqueue(std::make_unique<VirtFile>(reinterpret_cast<const u8*>(file->data() + member.offset), member.size,
                                       member.name, file->path(), file->tag));
  }
}

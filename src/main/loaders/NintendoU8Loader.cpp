/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NintendoU8Loader.h"

#include "LoaderManager.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::loaders {
LoaderRegistration<NintendoU8Loader> _nintendoU8("Nintendo U8");
}

void NintendoU8Loader::apply(const RawFile* file) {
  constexpr size_t kNodeSize = 12;
  if (file->size() < 0x20 || file->readWordBE(0) != 0x55aa382d) {
    return;
  }

  const size_t rootOffset = file->readWordBE(4);
  const size_t metadataSize = file->readWordBE(8);
  const size_t dataOffset = file->readWordBE(12);
  if (rootOffset < 0x20 || rootOffset > file->size() || metadataSize < kNodeSize ||
      metadataSize > file->size() - rootOffset || dataOffset < rootOffset + metadataSize ||
      dataOffset > file->size()) {
    return;
  }
  const u32 count = file->readWordBE(rootOffset + 8);
  if (file->readWordBE(rootOffset) != 0x01000000 || file->readWordBE(rootOffset + 4) != 0 ||
      count == 0 || count > metadataSize / kNodeSize) {
    return;
  }
  const size_t stringsOffset = rootOffset + static_cast<size_t>(count) * kNodeSize;
  const size_t stringsSize = rootOffset + metadataSize - stringsOffset;
  if (stringsSize == 0 || file->readByte(stringsOffset) != 0) {
    return;
  }

  struct Directory {
    u32 index;
    u32 end;
    std::string path;
  };
  struct Member {
    u32 offset;
    u32 size;
    std::string path;
    int priority;
  };
  std::vector<Directory> directories{{0, count, ""}};
  std::vector<Member> members;
  for (u32 index = 1; index < count; ++index) {
    while (index >= directories.back().end) {
      directories.pop_back();
    }
    const size_t nodeOffset = rootOffset + static_cast<size_t>(index) * kNodeSize;
    const u32 typeName = file->readWordBE(nodeOffset);
    const u32 offset = file->readWordBE(nodeOffset + 4);
    const u32 size = file->readWordBE(nodeOffset + 8);
    const u32 nameOffset = typeName & 0x00ffffff;
    if (nameOffset >= stringsSize) {
      return;
    }
    const char* name = file->data() + stringsOffset + nameOffset;
    const auto* end = static_cast<const char*>(std::memchr(name, 0, stringsSize - nameOffset));
    if (!end || end == name) {
      return;
    }
    const std::string component(name, end);
    if (component == ".." || component.find('/') != std::string::npos ||
        component.find('\\') != std::string::npos) {
      return;
    }
    const auto& parent = directories.back();
    const std::string path = component == "." ? parent.path : parent.path + component;
    if ((typeName >> 24) == 1) {
      if (offset != parent.index || size <= index || size > parent.end) {
        return;
      }
      directories.push_back({index, size, path.empty() || path.back() == '/' ? path : path + '/'});
    } else if ((typeName >> 24) == 0) {
      if (component == "." || offset < dataOffset || offset > file->size() || size > file->size() - offset) {
        return;
      }
      if (size == 0) {
        continue;
      }
      int priority = 2;
      if (size >= 0x50) {
        const u32 magic = file->readWordBE(offset) | 0x20202020;
        if (magic == 0x73776462 || magic == 0x7377646c) {
          const size_t pcmdOffset = static_cast<size_t>(offset) + 0x40;
          const u32 pcmdLength = magic == 0x73776462 ? file->readWordBE(pcmdOffset) : file->readWord(pcmdOffset);
          priority = pcmdLength == 0 || (pcmdLength & 0xffff0000) == 0xaaaa0000 ? 1 : 0;
        }
      }
      members.push_back({offset, size, path, priority});
    } else {
      return;
    }
  }

  // [Line Attack Heroes]: Shared sample banks must be available before the individual song banks and sequences.
  std::stable_sort(members.begin(), members.end(),
                   [](const Member& left, const Member& right) { return left.priority < right.priority; });
  // [Pokemon Fushigi no Dungeon: Ikuzo! Arashi no Boukendan]: The U8 data2 member is an AT7-compressed DSE payload.
  // [Pokemon Fushigi no Dungeon: Susume! Honou no Boukendan]: The U8 data2 member is an AT7-compressed DSE payload.
  // [Pokemon Fushigi no Dungeon: Mezase! Hikari no Boukendan]: The U8 data2 member is an AT7-compressed DSE payload.
  for (const auto& member : members) {
    enqueue(std::make_unique<VirtFile>(reinterpret_cast<const u8*>(file->data() + member.offset), member.size,
                                       member.path, file->path(), file->tag));
  }
}

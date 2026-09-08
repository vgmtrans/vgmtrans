/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NitroFsLoader.h"

#include "LoaderManager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace {
constexpr size_t kNdsHeaderSize = 0x200;
constexpr u32 kDarcMagic = 0x44415243;  // DARC
constexpr u32 kDencMagic = 0x44454e43;  // DENC
constexpr u32 kDsarMagic = 0x44534152;  // DSAR(C)

struct NitroDirectory {
  u32 subtableOffset;
  u16 firstFileId;
};

u32 readWordBE(const u8* data) {
  return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) | (static_cast<u32>(data[2]) << 8) |
         static_cast<u32>(data[3]);
}

u32 readWord(const u8* data, bool bigEndian) {
  if (bigEndian) {
    return readWordBE(data);
  }
  return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) | (static_cast<u32>(data[2]) << 16) |
         (static_cast<u32>(data[3]) << 24);
}

int dseLoadPriority(const u8* data, size_t size) {
  int bestPriority = 4;
  for (const u8 firstByte : {'s', 'S'}) {
    const u8* cursor = data;
    const u8* const end = data + size;
    while (end - cursor >= 0x10) {
      const auto* found = static_cast<const u8*>(std::memchr(cursor, firstByte, static_cast<size_t>(end - cursor)));
      if (!found || end - found < 0x10) {
        break;
      }
      cursor = found + 1;

      const u8 c1 = found[1] | 0x20;
      const u8 c2 = found[2] | 0x20;
      const u8 c3 = found[3] | 0x20;
      const bool sequence = c1 == 'm' && c2 == 'd';
      const bool bank = c1 == 'w' && c2 == 'd';
      if ((!sequence && !bank) || (c3 != 'l' && c3 != 'b')) {
        continue;
      }

      const size_t minimumSize = sequence ? 0x40 : 0x50;
      if (minimumSize > static_cast<size_t>(end - found)) {
        continue;
      }
      const u32 payloadSize = readWord(found + 8, c3 == 'b');
      if (payloadSize >= minimumSize && payloadSize <= static_cast<size_t>(end - found)) {
        if (sequence) {
          bestPriority = std::min(bestPriority, 3);
          continue;
        }

        const u32 pcmdLength = readWord(found + 0x40, c3 == 'b');
        // Sands of Destruction includes an empty BTL_SKILL.SWD (PCMD length
        // zero) before its external program banks. It provides no samples and
        // must not outrank the ROM.SWD bank those programs reference.
        if (pcmdLength != 0 && (pcmdLength & 0xffff0000) != 0xaaaa0000) {
          return 0;
        }
        bestPriority = std::min(bestPriority, 1);
      }
    }
  }
  return bestPriority;
}

bool hasDsePayload(const u8* data, size_t size) {
  return dseLoadPriority(data, size) != 4;
}

bool hasAudioPathHint(std::string name) {
  std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto contains = [&name](const char* needle) { return name.find(needle) != std::string::npos; };
  return contains("bgm") || contains("music") || contains("sound") || contains("audio") || contains("/se") ||
         contains("/wav");
}

bool isAudioCandidate(const u8* data, size_t size, const std::string& name) {
  if (size < 4) {
    return false;
  }

  const u32 magic = readWordBE(data);
  if (magic == kDsarMagic) {
    return true;
  }
  if ((magic == kDarcMagic || magic == kDencMagic) && (hasAudioPathHint(name) || hasDsePayload(data, size))) {
    return true;
  }

  std::string extension = std::filesystem::path(name).extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (extension == ".smd" || extension == ".swd" || extension == ".sed" || extension == ".sad" ||
      extension == ".dsarc" || extension == ".csnd") {
    return true;
  }

  return hasDsePayload(data, size);
}
}  // namespace

namespace vgmtrans::loaders {
LoaderRegistration<NitroFsLoader> _nitroFs("NitroFS");
}

void NitroFsLoader::apply(const RawFile* file) {
  const std::string extension = file->extension();
  if ((extension != "nds" && extension != "srl") || file->size() < kNdsHeaderSize) {
    return;
  }

  const u32 fntOffset = file->readWord(0x40);
  const u32 fntSize = file->readWord(0x44);
  const u32 fatOffset = file->readWord(0x48);
  const u32 fatSize = file->readWord(0x4c);
  if (fntSize < 8 || fntOffset > file->size() || fntSize > file->size() - fntOffset || fatSize == 0 ||
      fatSize % 8 != 0 || fatOffset > file->size() || fatSize > file->size() - fatOffset) {
    return;
  }

  const u32 fileCount = fatSize / 8;
  const u16 directoryCount = file->readShort(fntOffset + 6);
  if (fileCount == 0 || directoryCount == 0 || directoryCount > 0x1000 ||
      static_cast<size_t>(directoryCount) * 8 > fntSize) {
    return;
  }

  std::vector<NitroDirectory> directories;
  directories.reserve(directoryCount);
  for (u16 i = 0; i < directoryCount; ++i) {
    const size_t entryOffset = fntOffset + static_cast<size_t>(i) * 8;
    const u32 subtableOffset = file->readWord(entryOffset);
    const u16 firstFileId = file->readShort(entryOffset + 4);
    if (subtableOffset >= fntSize || firstFileId > fileCount) {
      return;
    }
    directories.push_back({subtableOffset, firstFileId});
  }

  std::vector<std::string> names(fileCount);
  std::vector<bool> visited(directoryCount);
  const size_t fntEnd = static_cast<size_t>(fntOffset) + fntSize;
  std::function<bool(u16, const std::string&)> readDirectory;
  readDirectory = [&](u16 directoryIndex, const std::string& parentPath) {
    if (directoryIndex >= directories.size() || visited[directoryIndex]) {
      return false;
    }
    visited[directoryIndex] = true;

    size_t cursor = static_cast<size_t>(fntOffset) + directories[directoryIndex].subtableOffset;
    u32 fileId = directories[directoryIndex].firstFileId;
    while (cursor < fntEnd) {
      const u8 typeAndLength = file->readByte(cursor++);
      if (typeAndLength == 0) {
        return true;
      }

      const size_t nameLength = typeAndLength & 0x7f;
      if (nameLength == 0 || nameLength > fntEnd - cursor) {
        return false;
      }
      const std::string entryName(file->data() + cursor, nameLength);
      cursor += nameLength;
      const std::string fullName = parentPath.empty() ? entryName : parentPath + "/" + entryName;

      if ((typeAndLength & 0x80) != 0) {
        if (fntEnd - cursor < sizeof(u16)) {
          return false;
        }
        const u16 directoryId = file->readShort(cursor);
        cursor += sizeof(u16);
        if ((directoryId & 0xf000) != 0xf000 || !readDirectory(directoryId & 0x0fff, fullName)) {
          return false;
        }
      } else {
        if (fileId >= names.size()) {
          return false;
        }
        names[fileId++] = fullName;
      }
    }
    return false;
  };

  if (!readDirectory(0, "")) {
    return;
  }

  struct Candidate {
    u32 offset;
    u32 size;
    std::string name;
    int loadPriority;
  };
  std::vector<Candidate> candidates;
  for (u32 id = 0; id < fileCount; ++id) {
    const size_t entryOffset = fatOffset + static_cast<size_t>(id) * 8;
    const u32 start = file->readWord(entryOffset);
    const u32 end = file->readWord(entryOffset + 4);
    if (start > end || end > file->size()) {
      return;
    }
    if (start == end) {
      continue;
    }

    std::string name = names[id];
    if (name.empty()) {
      name = fmt::format("file{:05}.bin", id);
    }
    const u32 size = end - start;
    const auto* data = reinterpret_cast<const u8*>(file->data() + start);
    if (isAudioCandidate(data, size, name)) {
      const u32 magic = readWordBE(data);
      const int loadPriority =
          magic == kDarcMagic || magic == kDencMagic || magic == kDsarMagic ? 2 : dseLoadPriority(data, size);
      candidates.push_back({start, size, std::move(name), loadPriority});
    }
  }

  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
    return left.loadPriority < right.loadPriority;
  });

  for (const auto& candidate : candidates) {
    const auto* data = reinterpret_cast<const u8*>(file->data() + candidate.offset);
    std::string name = candidate.name;
    const u32 magic = readWordBE(data);
    if (magic != kDarcMagic && magic != kDencMagic && magic != kDsarMagic && hasDsePayload(data, candidate.size)) {
      const std::string candidateExtension = std::filesystem::path(name).extension().string();
      if (candidateExtension != ".smd" && candidateExtension != ".SMD" && candidateExtension != ".swd" &&
          candidateExtension != ".SWD") {
        name += ".dse";
      }
    }
    enqueue(std::make_unique<VirtFile>(data, candidate.size, name, file->path(), file->tag));
  }
}

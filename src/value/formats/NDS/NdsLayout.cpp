/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsLayout.h"

#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr std::string_view kSdatSignature = "SDAT\xff\xfe\x00\x01";
constexpr u32 kMaxNameLength = 128;

[[nodiscard]] bool matches(ByteReader reader, u64 offset, std::string_view signature) {
  if (!reader.has(offset, signature.size())) {
    return false;
  }
  for (size_t i = 0; i < signature.size(); ++i) {
    if (reader.u8At(offset + i) != static_cast<u8>(signature[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string fallbackName(std::string_view prefix, u32 index) {
  return fmt::format("{}_{:04d}", prefix, index);
}

[[nodiscard]] std::string nullTerminatedString(ByteReader reader, u64 offset, u64 maxLength) {
  if (offset >= reader.size()) {
    return {};
  }
  const u64 limit = std::min<u64>(maxLength, reader.size() - offset);
  std::string result;
  result.reserve(static_cast<size_t>(limit));
  for (u64 i = 0; i < limit; ++i) {
    const u8 value = reader.u8At(offset + i);
    if (value == 0) {
      break;
    }
    result.push_back(static_cast<char>(value));
  }
  return result;
}

[[nodiscard]] u32 readCountFromInfoList(ByteReader reader, u32 infoOffset, u32 tablePointerField) {
  if (!reader.has(infoOffset + tablePointerField, 4)) {
    return 0;
  }
  const u32 listOffset = reader.le32(infoOffset + tablePointerField) + infoOffset;
  if (!reader.has(listOffset, 4)) {
    return 0;
  }
  return reader.le32(listOffset);
}

[[nodiscard]] std::vector<std::string> readNames(ByteReader reader, u32 symbOffset, u32 pointerListField, u32 count,
                                                 std::string_view fallbackPrefix, bool hasSymb) {
  std::vector<std::string> names;
  names.reserve(count);
  std::optional<u32> pointerList;
  if (hasSymb && reader.has(symbOffset + pointerListField, 4)) {
    pointerList = reader.le32(symbOffset + pointerListField) + symbOffset;
  }

  for (u32 i = 0; i < count; ++i) {
    std::string name;
    if (pointerList && reader.has(*pointerList + 4 + i * 4, 4)) {
      const u32 nameOffset = reader.le32(*pointerList + 4 + i * 4) + symbOffset;
      name = nullTerminatedString(reader, nameOffset, kMaxNameLength);
    }
    names.push_back(name.empty() ? fallbackName(fallbackPrefix, i) : std::move(name));
  }
  return names;
}

}  // namespace

std::vector<u32> findNdsSdatOffsets(ByteReader reader) {
  std::vector<u32> offsets;
  for (u64 offset = 0; offset + kSdatSignature.size() <= reader.size(); ++offset) {
    if (matches(reader, offset, kSdatSignature) && reader.has(offset + 0x10, 4) &&
        reader.le32(offset + 0x10) < 0x10000) {
      offsets.push_back(static_cast<u32>(offset));
    }
  }
  return offsets;
}

std::optional<NdsLayout> parseNdsLayout(ByteReader reader, u32 baseOffset) {
  // SDAT stores most offsets relative to section starts. Normalize them to source offsets
  // immediately so later parsers can use SourceRange directly.
  if (!matches(reader, baseOffset, kSdatSignature) || !reader.has(baseOffset + 0x24, 4)) {
    return std::nullopt;
  }

  NdsLayout layout{
      .baseOffset = baseOffset,
      .length = reader.le32(baseOffset + 8) + 8,
      .symbOffset = reader.le32(baseOffset + 0x10) + baseOffset,
      .infoOffset = reader.le32(baseOffset + 0x18) + baseOffset,
      .fatOffset = reader.le32(baseOffset + 0x20) + baseOffset,
  };
  layout.hasSymb = layout.symbOffset != baseOffset && reader.has(layout.symbOffset, 0x18);
  if (!reader.has(layout.infoOffset, 0x18) || !reader.has(layout.fatOffset, 0x0c)) {
    return std::nullopt;
  }

  const u32 sequenceCount = readCountFromInfoList(reader, layout.infoOffset, 0x08);
  const u32 bankCount = readCountFromInfoList(reader, layout.infoOffset, 0x10);
  const u32 waveArchiveCount = readCountFromInfoList(reader, layout.infoOffset, 0x14);

  layout.sequenceNames = readNames(reader, layout.symbOffset, 0x08, sequenceCount, "SSEQ", layout.hasSymb);
  layout.bankNames = readNames(reader, layout.symbOffset, 0x10, bankCount, "SBNK", layout.hasSymb);
  layout.waveArchiveNames = readNames(reader, layout.symbOffset, 0x14, waveArchiveCount, "SWAR", layout.hasSymb);

  const u32 sequenceInfoList = reader.le32(layout.infoOffset + 0x08) + layout.infoOffset;
  layout.sequences.reserve(sequenceCount);
  for (u32 i = 0; i < sequenceCount; ++i) {
    NdsSequenceInfo info;
    if (reader.has(sequenceInfoList + 4 + i * 4, 4)) {
      const u32 relative = reader.le32(sequenceInfoList + 4 + i * 4);
      const u32 offset = layout.infoOffset + relative;
      if (relative != 0 && reader.has(offset, 6)) {
        info.valid = true;
        info.fileId = reader.le16(offset);
        info.bank = reader.le16(offset + 4);
      } else if (reader.has(offset + 4, 2)) {
        info.bank = reader.le16(offset + 4);
      }
    }
    layout.sequences.push_back(info);
  }

  const u32 bankInfoList = reader.le32(layout.infoOffset + 0x10) + layout.infoOffset;
  layout.banks.reserve(bankCount);
  for (u32 i = 0; i < bankCount; ++i) {
    NdsBankInfo info;
    if (reader.has(bankInfoList + 4 + i * 4, 4)) {
      const u32 relative = reader.le32(bankInfoList + 4 + i * 4);
      const u32 offset = layout.infoOffset + relative;
      if (relative != 0 && reader.has(offset, 12)) {
        info.valid = true;
        info.fileId = reader.le16(offset);
        for (u32 j = 0; j < info.waveArchives.size(); ++j) {
          const u16 waveArchive = reader.le16(offset + 4 + j * 2);
          info.waveArchives[j] = waveArchive >= waveArchiveCount ? 0xffff : waveArchive;
        }
      }
    }
    layout.banks.push_back(info);
  }

  const u32 waveArchiveInfoList = reader.le32(layout.infoOffset + 0x14) + layout.infoOffset;
  layout.waveArchives.reserve(waveArchiveCount);
  for (u32 i = 0; i < waveArchiveCount; ++i) {
    NdsWaveArchiveInfo info;
    if (reader.has(waveArchiveInfoList + 4 + i * 4, 4)) {
      const u32 relative = reader.le32(waveArchiveInfoList + 4 + i * 4);
      const u32 offset = layout.infoOffset + relative;
      if (relative != 0 && reader.has(offset, 2)) {
        info.valid = true;
        info.fileId = reader.le16(offset);
      }
    }
    layout.waveArchives.push_back(info);
  }

  return layout;
}

std::optional<NdsFileRange> ndsFileRange(ByteReader reader, const NdsLayout& layout, u16 fileId) {
  const u64 fatEntry = static_cast<u64>(layout.fatOffset) + 12 + static_cast<u64>(fileId) * 0x10;
  if (!reader.has(fatEntry, 8)) {
    return std::nullopt;
  }

  const u32 offset = reader.le32(fatEntry) + layout.baseOffset;
  const u32 size = reader.le32(fatEntry + 4);
  if (!reader.has(offset, size)) {
    return std::nullopt;
  }

  return NdsFileRange{.offset = offset, .size = size};
}

}  // namespace vgmtrans::formats::nds

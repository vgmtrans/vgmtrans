/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/Nds.h"
#include "value/scan/BytePattern.h"

#include <fmt/format.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr std::string_view kSdatSignature = "SDAT\xff\xfe\x00\x01";
constexpr std::string_view kSseqSignature{"SSEQ\xff\xfe\x00\x01", 8};
constexpr u32 kMaxNameLength = 128;
constexpr u32 kSseqFileSizeOffset = 0x08;
constexpr u32 kSseqHeaderSize = 0x1c;

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

struct InfoRecord {
  u32 relativeOffset = 0;
  u32 sourceOffset = 0;
};

[[nodiscard]] std::optional<InfoRecord> infoRecord(ByteReader reader, u32 infoOffset, u32 listOffset, u32 index) {
  const u64 pointerOffset = static_cast<u64>(listOffset) + 4 + static_cast<u64>(index) * 4;
  if (!reader.has(pointerOffset, 4)) {
    return std::nullopt;
  }
  const u32 relative = reader.le32(pointerOffset);
  if (relative > std::numeric_limits<u32>::max() - infoOffset) {
    return std::nullopt;
  }
  return InfoRecord{.relativeOffset = relative, .sourceOffset = infoOffset + relative};
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

[[nodiscard]] std::optional<u32> nearbySseqHeader(ByteReader reader, u32 offset, u32 size) {
  constexpr u32 kMaxPaddingBeforeSseq = 0x200;
  const u64 searchEnd = std::min<u64>(reader.size(), static_cast<u64>(offset) + size + kMaxPaddingBeforeSseq);
  for (u64 candidate = offset + 1; candidate + kSseqSignature.size() <= searchEnd; ++candidate) {
    if (matchesBytes(reader, candidate, kSseqSignature)) {
      return static_cast<u32>(candidate);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool isZeroFilled(ByteReader reader, u32 begin, u32 end) {
  for (u32 offset = begin; offset < end && reader.has(offset, 1); ++offset) {
    if (reader.u8At(offset) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> recoveredMalformedSdatSequenceOffset(ByteReader reader, u32 offset, u32 size) {
  const auto sseqOffset = nearbySseqHeader(reader, offset, size);
  if (!sseqOffset) {
    return std::nullopt;
  }

  const u32 trackAddress = offset + kSseqHeaderSize;
  const u32 paddingEnd = std::min(*sseqOffset, offset + size);
  // Some zero-filled pseudo-sequences overlap a later SSEQ. If the padding
  // would align the SSEQ signature as bogus note data, leave it empty.
  if (size <= 0x100 && *sseqOffset >= trackAddress && isZeroFilled(reader, offset, paddingEnd) &&
      ((*sseqOffset - trackAddress) % 3) == 2) {
    return std::nullopt;
  }
  return sseqOffset;
}

}  // namespace

std::vector<u32> findNdsSdatOffsets(ByteReader reader) {
  std::vector<u32> offsets;
  for (u64 offset = 0; offset + kSdatSignature.size() <= reader.size(); ++offset) {
    if (matchesBytes(reader, offset, kSdatSignature) && reader.has(offset + 0x10, 4) &&
        reader.le32(offset + 0x10) < 0x10000) {
      offsets.push_back(static_cast<u32>(offset));
    }
  }
  return offsets;
}

std::optional<NdsLayout> parseNdsLayout(ByteReader reader, u32 baseOffset) {
  // SDAT stores most offsets relative to section starts. Normalize them to source offsets
  // immediately so later parsers can use SourceRange directly.
  if (!matchesBytes(reader, baseOffset, kSdatSignature) || !reader.has(baseOffset + 0x24, 4)) {
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
    if (const auto record = infoRecord(reader, layout.infoOffset, sequenceInfoList, i)) {
      if (record->relativeOffset != 0 && reader.has(record->sourceOffset, 6)) {
        info.valid = true;
        info.fileId = reader.le16(record->sourceOffset);
        info.bank = reader.le16(record->sourceOffset + 4);
      } else if (reader.has(record->sourceOffset + 4, 2)) {
        info.bank = reader.le16(record->sourceOffset + 4);
      }
    }
    layout.sequences.push_back(info);
  }

  const u32 bankInfoList = reader.le32(layout.infoOffset + 0x10) + layout.infoOffset;
  layout.banks.reserve(bankCount);
  for (u32 i = 0; i < bankCount; ++i) {
    NdsBankInfo info;
    if (const auto record = infoRecord(reader, layout.infoOffset, bankInfoList, i)) {
      if (record->relativeOffset != 0 && reader.has(record->sourceOffset, 12)) {
        info.valid = true;
        info.fileId = reader.le16(record->sourceOffset);
        for (u32 j = 0; j < info.waveArchives.size(); ++j) {
          const u16 waveArchive = reader.le16(record->sourceOffset + 4 + j * 2);
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
    if (const auto record = infoRecord(reader, layout.infoOffset, waveArchiveInfoList, i)) {
      if (record->relativeOffset != 0 && reader.has(record->sourceOffset, 2)) {
        info.valid = true;
        info.fileId = reader.le16(record->sourceOffset);
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

NdsSequenceRange ndsSequenceRangeForFatEntry(ByteReader reader, u32 offset, u32 size) {
  const bool hasSseqHeader = matchesBytes(reader, offset, kSseqSignature);
  const u32 fatEnd = static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + size));
  const std::optional<u32> recoveredSequenceOffset =
      hasSseqHeader ? std::nullopt : recoveredMalformedSdatSequenceOffset(reader, offset, size);
  const bool recoverMalformedSdatRange = recoveredSequenceOffset.has_value();
  const bool zeroFilled = !hasSseqHeader && !recoverMalformedSdatRange && isZeroFilled(reader, offset, fatEnd);
  const u32 sequenceOffset = recoveredSequenceOffset.value_or(offset);
  const u32 recoveredEnd = recoveredSequenceOffset && reader.has(*recoveredSequenceOffset + kSseqFileSizeOffset, 4)
                               ? static_cast<u32>(std::min<u64>(
                                     reader.size(), static_cast<u64>(*recoveredSequenceOffset) +
                                                        reader.le32(*recoveredSequenceOffset + kSseqFileSizeOffset)))
                               : static_cast<u32>(reader.size());
  const u32 emptySequenceEnd =
      static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + kSseqHeaderSize));

  u32 sequenceEnd = fatEnd;
  if (zeroFilled) {
    sequenceEnd = emptySequenceEnd;
  } else if (recoverMalformedSdatRange) {
    sequenceEnd = recoveredEnd;
  }

  return NdsSequenceRange{
      .offset = sequenceOffset,
      .sequenceEnd = sequenceEnd,
      .recoverMalformedSdatRange = recoverMalformedSdatRange,
  };
}

}  // namespace vgmtrans::formats::nds

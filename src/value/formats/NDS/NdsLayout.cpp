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

struct OffsetList {
  SourceRange range;
  u32 count = 0;
};

struct FatTable {
  SourceRange entries;
  u32 count = 0;
};

// Checks that a smaller byte range fits completely inside a larger one.
[[nodiscard]] bool contains(SourceRange bounds, u64 offset, u64 size) {
  return offset >= bounds.offset && offset <= bounds.endOffset() && size <= bounds.endOffset() - offset;
}

// Reads a section's location and size from the SDAT header. A section is returned
// only when all of its bytes are present inside this SDAT.
[[nodiscard]] std::optional<SourceRange> sectionRange(ByteReader reader, SourceRange sdat, u32 baseOffset,
                                                      u32 headerField, u32 minimumSize) {
  const u32 relativeOffset = reader.le32(baseOffset + headerField);
  const u32 size = reader.le32(baseOffset + headerField + 4);
  const u64 offset = static_cast<u64>(baseOffset) + relativeOffset;
  if (relativeOffset == 0 || size < minimumSize || !contains(sdat, offset, size) || !reader.has(offset, size)) {
    return std::nullopt;
  }
  return reader.range(offset, size);
}

// Finds one of the pointer lists stored in INFO or SYMB. The whole list must be
// present before its count is trusted, which keeps damaged files from causing
// unreasonable allocations.
[[nodiscard]] std::optional<OffsetList> offsetList(ScanResultBuilder& builder, SourceRange section, u32 pointerField,
                                                   std::string_view description) {
  const ByteReader reader = builder.reader();
  const u64 fieldOffset = section.offset + pointerField;
  if (!contains(section, fieldOffset, 4)) {
    return std::nullopt;
  }

  const u64 listOffset = section.offset + reader.le32(fieldOffset);
  if (!contains(section, listOffset, 4)) {
    builder.warning(fmt::format("NDS {} pointer was invalid", description), reader.range(fieldOffset, 4));
    return std::nullopt;
  }

  const u32 count = reader.le32(listOffset);
  const u64 size = 4 + static_cast<u64>(count) * 4;
  if (!contains(section, listOffset, size)) {
    builder.warning(fmt::format("NDS {} was truncated", description), reader.range(listOffset, 4));
    return std::nullopt;
  }
  return OffsetList{.range = reader.range(listOffset, size), .count = count};
}

// Follows one pointer from an INFO list to its record. Missing records are allowed;
// pointers outside INFO are rejected and reported.
[[nodiscard]] std::optional<u64> recordOffset(ScanResultBuilder& builder, SourceRange info, const OffsetList& list,
                                              u32 index, u32 size) {
  const ByteReader reader = builder.reader();
  const u64 pointerOffset = list.range.offset + 4 + static_cast<u64>(index) * 4;
  const u32 relativeOffset = reader.le32(pointerOffset);
  const u64 offset = info.offset + relativeOffset;
  if (relativeOffset == 0) {
    return std::nullopt;
  }
  if (!contains(info, offset, size)) {
    builder.warning("NDS INFO record pointer was invalid", reader.range(pointerOffset, 4));
    return std::nullopt;
  }
  return offset;
}

// Reads an asset name from SYMB. If the name is missing or unusable, a stable
// name such as SSEQ_0000 is returned instead.
[[nodiscard]] std::string nameAt(ByteReader reader, const std::optional<SourceRange>& symb,
                                 const std::optional<OffsetList>& names, u32 index, std::string_view prefix) {
  if (symb && names && index < names->count) {
    const u64 pointerOffset = names->range.offset + 4 + static_cast<u64>(index) * 4;
    const u64 nameOffset = symb->offset + reader.le32(pointerOffset);
    if (contains(*symb, nameOffset, 1)) {
      const u64 limit = std::min<u64>(kMaxNameLength, symb->endOffset() - nameOffset);
      const auto bytes = reader.slice(nameOffset, limit);
      const std::string name(bytes.begin(), std::ranges::find(bytes, u8{0}));
      if (!name.empty()) {
        return name;
      }
    }
  }
  return fmt::format("{}_{:04d}", prefix, index);
}

// Checks that every declared FAT entry is present, then exposes the bounded list
// used to look up the files stored in this SDAT.
[[nodiscard]] FatTable fatTable(ScanResultBuilder& builder, SourceRange fat) {
  const ByteReader reader = builder.reader();
  const u32 count = reader.le32(fat.offset + 8);
  const u64 size = static_cast<u64>(count) * 0x10;
  if (!contains(fat, fat.offset + 12, size)) {
    builder.warning("NDS FAT file table was truncated", reader.range(fat.offset + 8, 4));
    return {};
  }
  return FatTable{.entries = reader.range(fat.offset + 12, size), .count = count};
}

// Looks up a file ID in FAT and turns its SDAT-relative location into a safe
// range in the source file.
[[nodiscard]] std::optional<SourceRange> fileRange(ScanResultBuilder& builder, u32 baseOffset, const FatTable& fat,
                                                   u16 fileId, SourceRange fileIdRange) {
  const ByteReader reader = builder.reader();
  if (fileId >= fat.count) {
    builder.warning("NDS file ID was outside the FAT", fileIdRange);
    return std::nullopt;
  }

  const u64 entry = fat.entries.offset + static_cast<u64>(fileId) * 0x10;
  const u64 offset = static_cast<u64>(baseOffset) + reader.le32(entry);
  const u32 size = reader.le32(entry + 4);
  if (offset > std::numeric_limits<u32>::max() || size > std::numeric_limits<u32>::max() - offset ||
      !reader.has(offset, size)) {
    builder.warning("NDS FAT entry pointed outside the source", reader.range(entry, 8));
    return std::nullopt;
  }
  return reader.range(offset, size);
}

// Searches shortly beyond a bad FAT location for the SSEQ header that the entry
// was probably meant to reference.
[[nodiscard]] std::optional<u32> nearbySseqHeader(ByteReader reader, u32 offset, u32 size) {
  constexpr u32 kMaxPaddingBeforeSseq = 0x200;
  const u64 searchEnd = std::min<u64>(reader.size(), static_cast<u64>(offset) + size + kMaxPaddingBeforeSseq);
  for (u64 candidate = static_cast<u64>(offset) + 1; candidate + kSseqSignature.size() <= searchEnd; ++candidate) {
    if (matchesBytes(reader, candidate, kSseqSignature)) {
      return static_cast<u32>(candidate);
    }
  }
  return std::nullopt;
}

// Returns true when every available byte in the range is zero.
[[nodiscard]] bool isZeroFilled(ByteReader reader, u32 begin, u32 end) {
  for (u32 offset = begin; offset < end && reader.has(offset, 1); ++offset) {
    if (reader.u8At(offset) != 0) {
      return false;
    }
  }
  return true;
}

// Chooses a nearby SSEQ header for a malformed FAT entry, while avoiding a known
// kind of zero-filled placeholder that can otherwise look like sequence data.
[[nodiscard]] std::optional<u32> recoveredMalformedSdatSequenceOffset(ByteReader reader, u32 offset, u32 size) {
  const auto sseqOffset = nearbySseqHeader(reader, offset, size);
  if (!sseqOffset) {
    return std::nullopt;
  }

  const u32 trackAddress = offset + kSseqHeaderSize;
  const u32 paddingEnd = static_cast<u32>(std::min<u64>(*sseqOffset, static_cast<u64>(offset) + size));
  // Some zero-filled pseudo-sequences overlap a later SSEQ. If the padding
  // would align the SSEQ signature as bogus note data, leave it empty.
  if (size <= 0x100 && *sseqOffset >= trackAddress && isZeroFilled(reader, offset, paddingEnd) &&
      ((*sseqOffset - trackAddress) % 3) == 2) {
    return std::nullopt;
  }
  return sseqOffset;
}

}  // namespace

// Finds every plausible SDAT container embedded in the source.
std::vector<u32> findNdsSdatOffsets(ByteReader reader) {
  std::vector<u32> offsets;
  for (u64 offset = 0; offset <= std::numeric_limits<u32>::max() && offset + kSdatSignature.size() <= reader.size();
       ++offset) {
    if (matchesBytes(reader, offset, kSdatSignature) && reader.has(offset + 0x10, 4) &&
        reader.le32(offset + 0x10) < 0x10000) {
      offsets.push_back(static_cast<u32>(offset));
    }
  }
  return offsets;
}

// Reads one SDAT into named sequence, bank, and wave-archive entries. File ranges
// and relationships are resolved here so the module does not need to reopen the
// SDAT tables later.
std::optional<NdsLayout> parseNdsLayout(ScanResultBuilder& builder, u32 baseOffset) {
  const ByteReader reader = builder.reader();
  if (!matchesBytes(reader, baseOffset, kSdatSignature) || !reader.has(baseOffset, 0x28)) {
    const u64 warningOffset = std::min<u64>(baseOffset, reader.size());
    builder.warning("NDS SDAT header was invalid",
                    reader.range(warningOffset, std::min<u64>(0x28, reader.size() - warningOffset)));
    return std::nullopt;
  }

  const u64 declaredSize = static_cast<u64>(reader.le32(baseOffset + 8)) + 8;
  const u64 availableSize = reader.size() - baseOffset;
  const SourceRange sdat = reader.range(baseOffset, std::min(declaredSize, availableSize));
  const auto symb = sectionRange(reader, sdat, baseOffset, 0x10, 0x18);
  const auto info = sectionRange(reader, sdat, baseOffset, 0x18, 0x18);
  const auto fat = sectionRange(reader, sdat, baseOffset, 0x20, 0x0c);
  if (!info || !fat) {
    builder.warning("NDS SDAT section table was invalid", reader.range(baseOffset, 0x28));
    return std::nullopt;
  }

  const u32 headerSize = reader.le16(baseOffset + 0x0c);
  const u32 annotatedHeaderSize = headerSize >= 0x28 && reader.has(baseOffset, headerSize) ? headerSize : 0x28;
  builder.sourceMap()
      .header("SDAT Header", reader.range(baseOffset, annotatedHeaderSize))
      .field("file_size", reader.range(baseOffset + 8, 4), declaredSize, SourceValueDisplay::Hex)
      .field("symb_offset", reader.range(baseOffset + 0x10, 4), symb ? symb->offset : 0, SourceValueDisplay::Address)
      .field("info_offset", reader.range(baseOffset + 0x18, 4), info->offset, SourceValueDisplay::Address)
      .field("fat_offset", reader.range(baseOffset + 0x20, 4), fat->offset, SourceValueDisplay::Address);
  if (symb) {
    builder.sourceMap().section("SYMB Section", *symb).kind("sdat-symb");
  }
  builder.sourceMap().section("INFO Section", *info).kind("sdat-info");
  builder.sourceMap().table("FAT File Table", *fat).kind("sdat-fat");

  const auto sequences = offsetList(builder, *info, 0x08, "sequence INFO list");
  const auto banks = offsetList(builder, *info, 0x10, "bank INFO list");
  const auto waves = offsetList(builder, *info, 0x14, "wave-archive INFO list");
  const u32 sequenceCount = sequences ? sequences->count : 0;
  const u32 bankCount = banks ? banks->count : 0;
  const u32 waveCount = waves ? waves->count : 0;

  const auto sequenceNames = symb ? offsetList(builder, *symb, 0x08, "sequence name list") : std::nullopt;
  const auto bankNames = symb ? offsetList(builder, *symb, 0x10, "bank name list") : std::nullopt;
  const auto waveNames = symb ? offsetList(builder, *symb, 0x14, "wave-archive name list") : std::nullopt;
  const FatTable files = fatTable(builder, *fat);

  NdsLayout layout{.range = sdat};
  layout.sequences.reserve(sequenceCount);
  for (u32 index = 0; index < sequenceCount; ++index) {
    NdsSequenceInfo sequence{.name = nameAt(reader, symb, sequenceNames, index, "SSEQ")};
    if (const auto record = recordOffset(builder, *info, *sequences, index, 6)) {
      const u16 fileId = reader.le16(*record);
      sequence.file = fileRange(builder, baseOffset, files, fileId, reader.range(*record, 2));
      const u16 bank = reader.le16(*record + 4);
      if (bank < bankCount) {
        sequence.bank = bank;
      }
    }
    layout.sequences.push_back(std::move(sequence));
  }

  layout.banks.reserve(bankCount);
  for (u32 index = 0; index < bankCount; ++index) {
    NdsBankInfo bank{.name = nameAt(reader, symb, bankNames, index, "SBNK")};
    if (const auto record = recordOffset(builder, *info, *banks, index, 12)) {
      const u16 fileId = reader.le16(*record);
      bank.file = fileRange(builder, baseOffset, files, fileId, reader.range(*record, 2));
      for (u32 slot = 0; slot < bank.waveArchives.size(); ++slot) {
        const u16 wave = reader.le16(*record + 4 + slot * 2);
        if (wave < waveCount) {
          bank.waveArchives[slot] = wave;
        }
      }
    }
    layout.banks.push_back(std::move(bank));
  }

  layout.waveArchives.reserve(waveCount);
  for (u32 index = 0; index < waveCount; ++index) {
    NdsWaveArchiveInfo wave{.name = nameAt(reader, symb, waveNames, index, "SWAR")};
    if (const auto record = recordOffset(builder, *info, *waves, index, 2)) {
      const u16 fileId = reader.le16(*record);
      wave.file = fileRange(builder, baseOffset, files, fileId, reader.range(*record, 2));
    }
    layout.waveArchives.push_back(std::move(wave));
  }

  return layout;
}

// Chooses the usable sequence bounds for a normal file, an empty placeholder, or
// a malformed FAT entry that points just before the real SSEQ.
NdsSequenceRange ndsSequenceRangeForFatEntry(ByteReader reader, SourceRange file) {
  const u32 offset = static_cast<u32>(file.offset);
  const u32 size = static_cast<u32>(file.size);
  const u32 fatEnd = static_cast<u32>(std::min<u64>(reader.size(), file.endOffset()));
  if (matchesBytes(reader, offset, kSseqSignature)) {
    return NdsSequenceRange{.offset = offset, .sequenceEnd = fatEnd};
  }

  if (const auto recovered = recoveredMalformedSdatSequenceOffset(reader, offset, size)) {
    u32 sequenceEnd = static_cast<u32>(reader.size());
    if (reader.has(*recovered + kSseqFileSizeOffset, 4)) {
      sequenceEnd = static_cast<u32>(
          std::min<u64>(reader.size(), static_cast<u64>(*recovered) + reader.le32(*recovered + kSseqFileSizeOffset)));
    }
    return NdsSequenceRange{
        .offset = *recovered,
        .sequenceEnd = sequenceEnd,
        .recoverMalformedSdatRange = true,
    };
  }

  if (isZeroFilled(reader, offset, fatEnd)) {
    return NdsSequenceRange{
        .offset = offset,
        .sequenceEnd = static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + kSseqHeaderSize)),
    };
  }

  return NdsSequenceRange{.offset = offset, .sequenceEnd = fatEnd};
}

}  // namespace vgmtrans::formats::nds

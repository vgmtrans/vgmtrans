/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TamsoftPS1/TamsoftPS1.h"

#include "value/synth/PsxAdpcm.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <set>

namespace vgmtrans::formats::tamsoft_ps1 {

using namespace core;

namespace {

constexpr u32 kTrackRecordSize = 4;
constexpr u32 kPs1MaximumRequestSlots = 64;
constexpr u32 kPs2MusicVoiceCount = 36;
constexpr u32 kSilenceStream = 0x00fffff0;

[[nodiscard]] bool isSilence(ByteReader reader, u32 offset) {
  return reader.has(offset, 4) && reader.le32(offset) == kSilenceStream;
}

[[nodiscard]] bool validTrackTable(ByteReader reader, u32 offset, u32 records) {
  const u32 size = records * kTrackRecordSize;
  if (!reader.has(offset, size)) {
    return false;
  }
  for (u32 track = 0; track < records; ++track) {
    const u32 record = offset + track * kTrackRecordSize;
    const u8 priority = reader.u8At(record);
    const u16 relative = reader.le16(record + 2);
    if (reader.u8At(record + 1) != 0 || (priority & 0x7f) != 0 || relative < size ||
        !reader.has(offset + relative, 1)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> findSongTableEnd(ByteReader reader) {
  // The earliest nonzero entry target is also the end of the fixed-width table.
  u32 tableEnd = std::numeric_limits<u32>::max();
  for (u32 offset = 0; reader.has(offset, kSongEntrySize) && offset < tableEnd; offset += kSongEntrySize) {
    const u32 entry = reader.le32(offset);
    if (entry == kSilenceStream) {
      tableEnd = offset;
      break;
    }
    const u16 type = reader.le16(offset);
    const u16 relative = reader.le16(offset + 2);
    if (relative == 0) {
      if (type != 0) {
        return std::nullopt;
      }
      continue;
    }
    if (relative < offset + kSongEntrySize || !reader.has(relative, 1)) {
      return std::nullopt;
    }
    tableEnd = std::min(tableEnd, static_cast<u32>(relative));
  }
  if (tableEnd == std::numeric_limits<u32>::max() || tableEnd < kSongEntrySize ||
      tableEnd % kSongEntrySize != 0 || !isSilence(reader, tableEnd)) {
    return std::nullopt;
  }
  for (u32 offset = 0; offset < tableEnd; offset += kSongEntrySize) {
    const u32 entry = reader.le32(offset);
    if (entry == 0) {
      continue;
    }
    const u16 relative = reader.le16(offset + 2);
    if (relative < tableEnd || !reader.has(relative, 1)) {
      return std::nullopt;
    }
  }
  return tableEnd;
}

[[nodiscard]] Generation sequenceGeneration(ByteReader reader, u32 tableEnd) {
  for (u32 entryOffset = 0; entryOffset < tableEnd; entryOffset += kSongEntrySize) {
    const u16 relative = reader.le16(entryOffset + 2);
    if (reader.le16(entryOffset) == 0 && relative != 0 && !isSilence(reader, relative) &&
        validTrackTable(reader, relative, kPs2VoiceCount)) {
      return Generation::Ps2;
    }
  }
  // HG2's two SFX-only files have 100 request slots; Wonderful's largest
  // table has 63. BGM files are identified structurally above.
  return tableEnd / kSongEntrySize > kPs1MaximumRequestSlots ? Generation::Ps2 : Generation::Ps1;
}

}  // namespace

std::vector<SequenceLayout> readSequenceLayouts(ByteReader reader) {
  const auto tableEnd = findSongTableEnd(reader);
  if (!tableEnd) {
    return {};
  }
  const Generation generation = sequenceGeneration(reader, *tableEnd);
  std::vector<SequenceLayout> layouts;
  for (u32 entryOffset = 0; entryOffset < *tableEnd; entryOffset += kSongEntrySize) {
    if (reader.le32(entryOffset) == 0) {
      continue;
    }
    const u16 type = reader.le16(entryOffset);
    const u32 target = reader.le16(entryOffset + 2);
    if (isSilence(reader, target)) {
      continue;
    }

    SequenceLayout layout{
        .song = entryOffset / kSongEntrySize,
        .type = type,
        .tableSize = *tableEnd,
        .headerOffset = target,
        .generation = generation,
    };
    if (type != 0) {
      layout.tracks.push_back(TrackLayout{.offset = target});
      layouts.push_back(std::move(layout));
      continue;
    }

    const u32 records = generation == Generation::Ps2 ? kPs2VoiceCount : kPs1VoiceCount;
    if (!validTrackTable(reader, target, records)) {
      continue;
    }
    layout.headerSize = records * kTrackRecordSize;
    // HG2 stores 48 records but reqmus initializes only voices 8..43; the PS1
    // driver consumes all 24 records.
    const u32 playedRecords = generation == Generation::Ps2 ? kPs2MusicVoiceCount : kPs1VoiceCount;
    for (u32 slot = 0; slot < playedRecords; ++slot) {
      const u32 record = target + slot * kTrackRecordSize;
      const u32 trackOffset = target + reader.le16(record + 2);
      if (isSilence(reader, trackOffset)) {
        continue;
      }
      layout.tracks.push_back(TrackLayout{
          .slot = slot,
          .offset = trackOffset,
      });
    }
    if (!layout.tracks.empty()) {
      layouts.push_back(std::move(layout));
    }
  }
  return layouts;
}

std::optional<BankLayout> readBankLayout(ByteReader reader) {
  if (!reader.has(0, kBankHeaderSize)) {
    return std::nullopt;
  }
  const u32 sampleSize = reader.le32(kProgramTableSize - 4);
  if (sampleSize == 0 || static_cast<u64>(kBankHeaderSize) + sampleSize != reader.size()) {
    return std::nullopt;
  }

  std::set<u32> offsets;
  u32 nonzeroAdsr = 0;
  u32 ps2Adsr = 0;
  for (u32 program = 0; program < kProgramCount; ++program) {
    const u32 sample = reader.le32(program * 4);
    if (sample > sampleSize || (sample & 0x0f) != 0) {
      return std::nullopt;
    }
    if (sample != 0 && sample < sampleSize) {
      offsets.insert(sample);
    }
    const u32 adsr = reader.le32(kProgramTableSize + program * 4);
    if (adsr != 0) {
      ++nonzeroAdsr;
      if ((static_cast<u16>(adsr) & 0xfff0) == 0xe110 && (adsr >> 24) == 0xd2) {
        ++ps2Adsr;
      }
    }
  }
  if (nonzeroAdsr == 0) {
    return std::nullopt;
  }

  if (!offsets.empty()) {
    bool hasSample = false;
    for (auto current = offsets.begin(); current != offsets.end(); ++current) {
      const auto next = std::next(current);
      const u32 end = kBankHeaderSize + (next == offsets.end() ? sampleSize : *next);
      if (inspectPsxAdpcmStream(reader, kBankHeaderSize + *current, end)) {
        hasSample = true;
        break;
      }
    }
    if (!hasSample) {
      return std::nullopt;
    }
  }

  return BankLayout{
      .sampleSize = sampleSize,
      .generation = ps2Adsr * 4 >= nonzeroAdsr * 3 ? Generation::Ps2 : Generation::Ps1,
  };
}

}  // namespace vgmtrans::formats::tamsoft_ps1

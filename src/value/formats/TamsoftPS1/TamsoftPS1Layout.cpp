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

constexpr u32 kBankHeaderSize = 0x800;
constexpr u32 kSilenceStream = 0x00fffff0;

[[nodiscard]] bool isSilence(ByteReader reader, u32 offset) {
  return reader.has(offset, 4) && reader.le32(offset) == kSilenceStream;
}

[[nodiscard]] bool validBgmHeader(ByteReader reader, u32 offset, u32 records) {
  const u32 size = records * 4;
  if (!reader.has(offset, size)) {
    return false;
  }
  for (u32 track = 0; track < records; ++track) {
    const u32 record = offset + track * 4;
    const u8 priority = reader.u8At(record);
    const u16 relative = reader.le16(record + 2);
    if (reader.u8At(record + 1) != 0 || (priority & 0x7f) != 0 || relative < size ||
        !reader.has(offset + relative, 1)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> songTableSize(ByteReader reader) {
  u32 firstData = std::numeric_limits<u32>::max();
  for (u32 offset = 0; reader.has(offset, 4) && offset < firstData; offset += 4) {
    const u32 entry = reader.le32(offset);
    if (entry == kSilenceStream) {
      firstData = offset;
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
    if (relative < offset + 4 || !reader.has(relative, 1)) {
      return std::nullopt;
    }
    firstData = std::min(firstData, static_cast<u32>(relative));
  }
  if (firstData == std::numeric_limits<u32>::max() || firstData < 4 || (firstData & 3) != 0 ||
      !isSilence(reader, firstData)) {
    return std::nullopt;
  }
  for (u32 offset = 0; offset < firstData; offset += 4) {
    const u32 entry = reader.le32(offset);
    if (entry == 0) {
      continue;
    }
    const u16 relative = reader.le16(offset + 2);
    if (relative < firstData || !reader.has(relative, 1)) {
      return std::nullopt;
    }
  }
  return firstData;
}

[[nodiscard]] Generation sequenceGeneration(ByteReader reader, u32 tableSize) {
  for (u32 entry = 0; entry < tableSize; entry += 4) {
    const u16 relative = reader.le16(entry + 2);
    if (reader.le16(entry) == 0 && relative != 0 && !isSilence(reader, relative) &&
        validBgmHeader(reader, relative, 48)) {
      return Generation::Ps2;
    }
  }
  // HG2's two SFX-only files have 100 request slots; Wonderful's largest
  // table has 63. BGM files are identified structurally above.
  return tableSize / 4 > 64 ? Generation::Ps2 : Generation::Ps1;
}

}  // namespace

std::vector<SequenceLayout> readSequenceLayouts(ByteReader reader) {
  const auto tableSize = songTableSize(reader);
  if (!tableSize) {
    return {};
  }
  const Generation generation = sequenceGeneration(reader, *tableSize);
  std::vector<SequenceLayout> layouts;
  for (u32 entry = 0; entry < *tableSize; entry += 4) {
    const u32 encoded = reader.le32(entry);
    if (encoded == 0) {
      continue;
    }
    const u16 type = reader.le16(entry);
    const u32 target = reader.le16(entry + 2);
    if (isSilence(reader, target)) {
      continue;
    }

    SequenceLayout layout{
        .song = entry / 4,
        .type = type,
        .tableSize = *tableSize,
        .headerOffset = target,
        .generation = generation,
    };
    if (type != 0) {
      layout.tracks.push_back(TrackLayout{.offset = target, .priority = 1});
      layouts.push_back(std::move(layout));
      continue;
    }

    const u32 records = generation == Generation::Ps2 ? 48 : 24;
    if (!validBgmHeader(reader, target, records)) {
      continue;
    }
    layout.headerSize = records * 4;
    // HG2 stores 48 records but reqmus initializes only voices 8..43. The
    // PS1 driver consumes all 24 records and promotes a zero priority to one.
    const u32 playedRecords = generation == Generation::Ps2 ? 36 : 24;
    for (u32 slot = 0; slot < playedRecords; ++slot) {
      const u32 record = target + slot * 4;
      const u32 trackOffset = target + reader.le16(record + 2);
      if (isSilence(reader, trackOffset)) {
        continue;
      }
      const u8 encodedPriority = reader.u8At(record);
      layout.tracks.push_back(TrackLayout{
          .slot = slot,
          .headerOffset = record,
          .offset = trackOffset,
          .priority = encodedPriority == 0 ? static_cast<u8>(1) : encodedPriority,
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
  const u32 sampleSize = reader.le32(0x3fc);
  if (sampleSize == 0 || static_cast<u64>(kBankHeaderSize) + sampleSize != reader.size()) {
    return std::nullopt;
  }

  std::set<u32> offsets;
  u32 nonzeroAdsr = 0;
  u32 ps2Adsr = 0;
  for (u32 program = 0; program < 256; ++program) {
    const u32 sample = reader.le32(program * 4);
    if (sample > sampleSize || (sample & 0x0f) != 0) {
      return std::nullopt;
    }
    if (sample != 0 && sample < sampleSize) {
      offsets.insert(sample);
    }
    const u32 adsr = reader.le32(0x400 + program * 4);
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
      const u32 end = kBankHeaderSize +
                      (std::next(current) == offsets.end() ? sampleSize : *std::next(current));
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

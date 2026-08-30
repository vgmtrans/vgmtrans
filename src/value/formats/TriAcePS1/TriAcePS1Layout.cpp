/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TriAcePS1/TriAcePS1.h"

#include <algorithm>
#include <limits>

namespace vgmtrans::formats::triace_ps1 {

using namespace core;

namespace {

constexpr u32 kSequenceHeaderSize = 0xd6;
constexpr u32 kTrackTable = 0x16;
constexpr u32 kTrackCount = 32;
constexpr u32 kTrackRecordSize = 6;
constexpr u32 kBankHeaderSize = 12;
constexpr u32 kInstrumentHeaderSize = 8;
constexpr u32 kRegionSize = 20;

}  // namespace

std::optional<TriAcePs1SequenceLayout> readTriAcePs1SequenceLayout(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kSequenceHeaderSize) || reader.le16(offset) != 0xffff) {
    return std::nullopt;
  }

  const u32 length = static_cast<u32>(reader.le16(offset + 2)) + 2;
  const u8 tempo = reader.u8At(offset + 0x0f);
  if (length < kSequenceHeaderSize || !reader.has(offset, length) || tempo == 0 || tempo > 240) {
    return std::nullopt;
  }

  TriAcePs1SequenceLayout layout{
      .offset = offset,
      .length = length,
      .tempo = tempo,
      .timeSignatureNumerator = reader.u8At(offset + 0x10),
      .timeSignatureDenominator = reader.u8At(offset + 0x11),
  };
  const u32 end = offset + length;
  for (u32 slot = 0; slot < kTrackCount; ++slot) {
    const u32 record = offset + kTrackTable + slot * kTrackRecordSize;
    const u16 relativePlaylist = reader.le16(record + 4);
    if (relativePlaylist == 0) {
      continue;
    }
    const u32 playlist = offset + relativePlaylist;
    if (playlist < offset + kSequenceHeaderSize || playlist >= end) {
      return std::nullopt;
    }

    TriAcePs1TrackLayout track{
        .slot = static_cast<u8>(slot),
        .recordOffset = record,
        .unknown1 = reader.le16(record),
        .unknown2 = reader.le16(record + 2),
        .playlistOffset = playlist,
    };
    u32 cursor = playlist;
    while (cursor + 2 <= end && reader.le16(cursor) != 0xffff) {
      const u32 pattern = offset + reader.le16(cursor);
      if (pattern < offset + kSequenceHeaderSize || pattern >= end) {
        return std::nullopt;
      }
      track.patternAddresses.push_back(pattern);
      cursor += 2;
    }
    if (track.patternAddresses.empty() || cursor + 2 > end || reader.le16(cursor) != 0xffff) {
      return std::nullopt;
    }
    track.playlistLength = cursor + 2 - playlist;
    layout.tracks.push_back(std::move(track));
  }
  if (layout.tracks.empty()) {
    return std::nullopt;
  }
  return layout;
}

std::vector<TriAcePs1SequenceLayout> findTriAcePs1Sequences(ByteReader reader) {
  std::vector<TriAcePs1SequenceLayout> layouts;
  for (u64 offset = 0; offset + kSequenceHeaderSize <= reader.size(); ++offset) {
    if (reader.le16(offset) != 0xffff || offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    if (auto layout = readTriAcePs1SequenceLayout(reader, static_cast<u32>(offset))) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      offset += length - 1;
    }
  }
  return layouts;
}

std::optional<TriAcePs1BankLayout> readTriAcePs1BankLayout(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kBankHeaderSize)) {
    return std::nullopt;
  }
  const u32 length = reader.le32(offset);
  const u16 instrumentSize = reader.le16(offset + 4);
  if (instrumentSize < kBankHeaderSize + 4 || instrumentSize >= length || !reader.has(offset, length) ||
      !reader.has(offset + instrumentSize, 16)) {
    return std::nullopt;
  }
  for (u32 i = 0; i < 16; ++i) {
    if (reader.u8At(offset + instrumentSize + i) != 0) {
      return std::nullopt;
    }
  }
  if (reader.le32(offset + instrumentSize - 4) != 0xffffffff) {
    return std::nullopt;
  }

  u32 cursor = offset + kBankHeaderSize;
  const u32 terminator = offset + instrumentSize - 4;
  u32 instruments = 0;
  while (cursor < terminator) {
    if (!reader.has(cursor, kInstrumentHeaderSize)) {
      return std::nullopt;
    }
    const u8 regions = reader.u8At(cursor + 7);
    const u64 recordSize = kInstrumentHeaderSize + static_cast<u64>(regions) * kRegionSize;
    if (regions == 0 || recordSize > terminator - cursor) {
      return std::nullopt;
    }
    cursor += static_cast<u32>(recordSize);
    ++instruments;
  }
  if (cursor != terminator || instruments == 0) {
    return std::nullopt;
  }

  return TriAcePs1BankLayout{
      .offset = offset,
      .length = length,
      .instrumentSectionSize = instrumentSize,
      .unknown06 = reader.le16(offset + 6),
      .unknown08 = reader.le16(offset + 8),
      .unknown0a = reader.le16(offset + 10),
      .sampleSectionOffset = offset + instrumentSize,
      .sampleSectionSize = length - instrumentSize,
  };
}

std::vector<TriAcePs1BankLayout> findTriAcePs1Banks(ByteReader reader, u32 begin, std::optional<u32> length) {
  std::vector<TriAcePs1BankLayout> layouts;
  if (begin > reader.size()) {
    return layouts;
  }
  const u64 available = reader.size() - begin;
  const u64 scanLength = length ? std::min<u64>(*length, available) : available;
  const u64 end = static_cast<u64>(begin) + scanLength;
  for (u64 offset = begin; offset + kBankHeaderSize <= end; ++offset) {
    if (offset > std::numeric_limits<u32>::max()) {
      break;
    }
    if (auto layout = readTriAcePs1BankLayout(reader, static_cast<u32>(offset));
        layout && offset + layout->length <= end) {
      const u32 bankLength = layout->length;
      layouts.push_back(std::move(*layout));
      offset += bankLength - 1;
    }
  }
  return layouts;
}

}  // namespace vgmtrans::formats::triace_ps1

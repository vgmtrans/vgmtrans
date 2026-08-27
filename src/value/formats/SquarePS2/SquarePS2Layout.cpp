/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SquarePS2/SquarePS2.h"

#include <algorithm>
#include <limits>

namespace vgmtrans::formats::square_ps2 {

using namespace core;

namespace {

constexpr u32 kBgmSignature = 0x204d4742;  // "BGM "
constexpr u16 kWdSignature = 0x4457;       // "WD"
constexpr u32 kBgmHeaderSize = 0x20;
constexpr u32 kWdHeaderSize = 0x20;
constexpr u32 kRegionSize = 0x20;

[[nodiscard]] bool validRange(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

[[nodiscard]] u32 align4(u32 value) {
  return (value + 3) & ~u32{3};
}

}  // namespace

std::optional<BgmLayout> readBgmLayout(ByteReader reader, u32 offset) {
  if (!validRange(reader, offset, kBgmHeaderSize) || reader.le32(offset) != kBgmSignature) {
    return std::nullopt;
  }
  BgmLayout layout{
      .offset = offset,
      .declaredLength = reader.le32(offset + 0x10),
      .sequenceId = reader.le16(offset + 4),
      .waveBankId = reader.le16(offset + 6),
      .trackCount = reader.u8At(offset + 8),
      .initialTempo = reader.le16(offset + 0x0a),
      .initialMasterLevel = static_cast<u8>(reader.u8At(offset + 0x0c) & 0x7f),
      .ppqn = reader.le16(offset + 0x0e),
      .flags = reader.le32(offset + 0x14),
  };
  if (layout.declaredLength < kBgmHeaderSize || layout.trackCount == 0 || layout.trackCount > 48) {
    return std::nullopt;
  }
  layout.length = static_cast<u32>(std::min<u64>(layout.declaredLength, reader.size() - offset));
  if (layout.ppqn == 0 || layout.ppqn > 960) {
    layout.ppqn = 48;
  }

  u32 track = offset + kBgmHeaderSize;
  const u32 end = offset + layout.length;
  layout.tracks.reserve(layout.trackCount);
  for (u32 i = 0; i < layout.trackCount; ++i) {
    if (track > end || end - track < 4) {
      break;
    }
    const u32 length = reader.le32(track);
    if (length == 0) {
      break;
    }
    const u32 available = end - track - 4;
    const u32 boundedLength = std::min(length, available);
    if (boundedLength == 0) {
      break;
    }
    layout.tracks.push_back(BgmTrackLayout{.blockOffset = track, .dataOffset = track + 4, .length = boundedLength});
    track += 4 + boundedLength;
    if (boundedLength != length) {
      break;
    }
  }
  if (layout.tracks.empty()) {
    return std::nullopt;
  }
  return layout;
}

std::vector<BgmLayout> findBgmLayouts(ByteReader reader) {
  std::vector<BgmLayout> layouts;
  for (u64 offset = 0; offset + kBgmHeaderSize <= reader.size(); ++offset) {
    if (reader.le32(offset) != kBgmSignature || offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    if (auto layout = readBgmLayout(reader, static_cast<u32>(offset))) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      offset += length - 1;
    }
  }
  return layouts;
}

std::optional<WdLayout> readWdLayout(ByteReader reader, u32 offset) {
  if (!validRange(reader, offset, kWdHeaderSize) || reader.le16(offset) != kWdSignature) {
    return std::nullopt;
  }
  WdLayout layout{
      .offset = offset,
      .bankId = reader.le16(offset + 2),
      .sampleSize = reader.le32(offset + 4),
      .instrumentCount = reader.le32(offset + 8),
      .regionCount = reader.le32(offset + 0x0c),
      .instrumentTableOffset = offset + kWdHeaderSize,
  };
  if (layout.sampleSize < 16 || layout.instrumentCount == 0 || layout.instrumentCount > 256 ||
      layout.regionCount == 0 || layout.regionCount > 65536) {
    return std::nullopt;
  }
  layout.regionTableOffset = layout.instrumentTableOffset + align4(layout.instrumentCount) * 4;
  const u64 sampleOffset =
      static_cast<u64>(layout.regionTableOffset) + static_cast<u64>(layout.regionCount) * kRegionSize;
  const u64 length = sampleOffset - offset + layout.sampleSize;
  if (sampleOffset > std::numeric_limits<u32>::max() || length > std::numeric_limits<u32>::max() ||
      !validRange(reader, offset, length)) {
    return std::nullopt;
  }
  layout.sampleOffset = static_cast<u32>(sampleOffset);
  layout.length = static_cast<u32>(length);
  for (u32 i = 0; i < layout.instrumentCount; ++i) {
    const u32 relative = reader.le32(layout.instrumentTableOffset + i * 4);
    if (relative != 0 && (relative < layout.regionTableOffset - offset || relative >= layout.sampleOffset - offset ||
                          (relative - (layout.regionTableOffset - offset)) % kRegionSize != 0)) {
      return std::nullopt;
    }
  }
  return layout;
}

std::vector<WdLayout> findWdLayouts(ByteReader reader) {
  std::vector<WdLayout> layouts;
  for (u64 offset = 0; offset + kWdHeaderSize <= reader.size(); ++offset) {
    if (reader.le16(offset) != kWdSignature || offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    if (auto layout = readWdLayout(reader, static_cast<u32>(offset))) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      offset += length - 1;
    }
  }
  return layouts;
}

}  // namespace vgmtrans::formats::square_ps2

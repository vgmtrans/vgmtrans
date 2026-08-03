/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiPS1/SuzukiPS1.h"

#include <algorithm>
#include <limits>

namespace vgmtrans::formats::suzuki_ps1 {

using namespace core;

namespace {

constexpr u32 kSequenceSignature = 0x73646d73;  // "smds", little endian
constexpr u32 kDwdsSignature = 0x73647764;      // "dwds"
constexpr u32 kWdsSignature = 0x20736477;       // "wds "
constexpr u32 kSequenceHeaderSize = 0x22;
constexpr u32 kBankHeaderSize = 0x30;
constexpr u32 kInstrumentRecordSize = 0x10;

[[nodiscard]] bool rangeValid(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

}  // namespace

std::optional<SuzukiPs1SequenceLayout> readSuzukiPs1SequenceLayout(ByteReader reader, u32 offset) {
  if (!rangeValid(reader, offset, kSequenceHeaderSize) || reader.le32(offset) != kSequenceSignature) {
    return std::nullopt;
  }

  SuzukiPs1SequenceLayout layout{
      .offset = offset,
      .length = reader.le16(offset + 0x08),
      .trackCount = reader.u8At(offset + 0x14),
      .percussionCount = reader.u8At(offset + 0x15),
      .defaultBank = reader.le16(offset + 0x16),
      .titleOffset = reader.le16(offset + 0x1e),
      .percussionOffset = reader.le16(offset + 0x20),
  };
  if (layout.length < kSequenceHeaderSize || !rangeValid(reader, offset, layout.length) || layout.trackCount == 0 ||
      !rangeValid(reader, offset + kSequenceHeaderSize, layout.trackCount * 2ull) ||
      layout.titleOffset < kSequenceHeaderSize + layout.trackCount * 2u || layout.titleOffset >= layout.length ||
      layout.percussionOffset < layout.titleOffset || layout.percussionOffset > layout.length) {
    return std::nullopt;
  }

  const u32 titleBytes = layout.percussionOffset - layout.titleOffset;
  if (titleBytes != 0) {
    const auto bytes = reader.slice(offset + layout.titleOffset, titleBytes);
    const auto nul = std::ranges::find(bytes, u8{0});
    layout.title.assign(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::size_t>(std::distance(bytes.begin(), nul)));
  }

  layout.trackAddresses.reserve(layout.trackCount);
  for (u32 i = 0; i < layout.trackCount; ++i) {
    const u32 relative = reader.le16(offset + kSequenceHeaderSize + i * 2);
    if (relative >= layout.length || !reader.has(offset + relative, 1)) {
      return std::nullopt;
    }
    layout.trackAddresses.push_back(offset + relative);
  }
  return layout;
}

std::vector<SuzukiPs1SequenceLayout> findSuzukiPs1Sequences(ByteReader reader) {
  std::vector<SuzukiPs1SequenceLayout> layouts;
  for (u64 offset = 0; offset + kSequenceHeaderSize <= reader.size(); ++offset) {
    if (reader.le32(offset) != kSequenceSignature || offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    if (auto layout = readSuzukiPs1SequenceLayout(reader, static_cast<u32>(offset))) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      offset += length - 1;
    }
  }
  return layouts;
}

std::optional<SuzukiPs1BankLayout> readSuzukiPs1BankLayout(ByteReader reader, u32 offset) {
  if (!rangeValid(reader, offset, kBankHeaderSize)) {
    return std::nullopt;
  }
  const u32 signature = reader.le32(offset);
  if (signature != kDwdsSignature && signature != kWdsSignature) {
    return std::nullopt;
  }

  const u32 totalSize = reader.le32(offset + 0x08);
  const u32 headerSize = reader.le32(offset + 0x10);
  const u32 sampleSize = reader.le32(offset + 0x14);
  const u32 highestProgram = reader.le32(offset + 0x1c);
  const u32 bank = reader.le32(offset + 0x20);
  const u64 minimumHeader = kBankHeaderSize + (static_cast<u64>(highestProgram) + 1) * kInstrumentRecordSize;
  const u64 length = static_cast<u64>(headerSize) + sampleSize;
  if (headerSize < minimumHeader || highestProgram > 255 || bank > 0xffff || length > totalSize ||
      length > std::numeric_limits<u32>::max() || !rangeValid(reader, offset, length) || sampleSize < 16 ||
      !reader.has(offset + headerSize, 16)) {
    return std::nullopt;
  }

  // Real banks begin their SPU upload with the driver's silent ADPCM block.
  // This avoids accepting unrelated text or tables that happen to contain WDS.
  for (u32 i = 0; i < 16; ++i) {
    if (reader.u8At(offset + headerSize + i) != 0) {
      return std::nullopt;
    }
  }
  return SuzukiPs1BankLayout{
      .offset = offset,
      .length = static_cast<u32>(length),
      .headerSize = headerSize,
      .sampleSize = sampleSize,
      .bank = static_cast<u16>(bank),
      .highestProgram = static_cast<u16>(highestProgram),
      .kind = signature == kDwdsSignature ? SuzukiPs1BankKind::Dwds : SuzukiPs1BankKind::Wds,
  };
}

std::vector<SuzukiPs1BankLayout> findSuzukiPs1Banks(ByteReader reader) {
  std::vector<SuzukiPs1BankLayout> layouts;
  for (u64 offset = 0; offset + kBankHeaderSize <= reader.size(); ++offset) {
    const u32 signature = reader.le32(offset);
    if ((signature != kDwdsSignature && signature != kWdsSignature) || offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    if (auto layout = readSuzukiPs1BankLayout(reader, static_cast<u32>(offset))) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      offset += length - 1;
    }
  }
  return layouts;
}

}  // namespace vgmtrans::formats::suzuki_ps1

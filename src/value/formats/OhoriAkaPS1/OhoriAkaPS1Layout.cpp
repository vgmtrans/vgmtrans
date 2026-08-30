/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OhoriAkaPS1/OhoriAkaPS1.h"

#include "value/synth/PsxAdpcm.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace vgmtrans::formats::ohori_aka_ps1 {

using namespace core;

namespace {

constexpr u32 kSequenceSignature = 0x41534f48;  // "HOSA"
constexpr u32 kSequenceHeaderSize = 0x50;
constexpr u32 kRegionSize = 0x10;
constexpr u32 kMaximumTracks = 24;
constexpr u32 kMaximumInstruments = 256;
constexpr u32 kMaximumSequenceSize = 0x100000;
constexpr std::array<u8, 32> kControlParameters{
    0, 1, 1, 1, 1, 1, 1, 2, 4, 1, 2, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

[[nodiscard]] bool rangeValid(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

[[nodiscard]] std::optional<u32> skipVariable(ByteReader reader, u32 cursor, u32 end) {
  if (cursor >= end || !reader.has(cursor, 1)) {
    return std::nullopt;
  }
  return (reader.u8At(cursor) & 0x80) != 0 && cursor + 1 < end ? cursor + 2 : cursor + 1;
}

[[nodiscard]] std::optional<u32> eventEnd(ByteReader reader, u32 cursor, u32 end) {
  if (cursor >= end || !reader.has(cursor, 1)) {
    return std::nullopt;
  }
  const u8 status = reader.u8At(cursor++);
  if (status < 0x80) {
    if (cursor >= end) {
      return std::nullopt;
    }
    const u8 note = reader.u8At(cursor++);
    const u8 timing = status & 0x60;
    if (timing == 0x40) {
      const auto next = skipVariable(reader, cursor, end);
      if (!next) return std::nullopt;
      cursor = *next;
    } else if (timing == 0x60) {
      ++cursor;
    }
    if ((status & 0x1f) == 0x1f) {
      const auto next = skipVariable(reader, cursor, end);
      if (!next) return std::nullopt;
      cursor = *next;
    }
    if ((note & 0x80) != 0) {
      ++cursor;
    }
    return cursor <= end ? std::optional(cursor) : std::nullopt;
  }

  // 0xa0-0xbf are relative notes, not controls.
  if ((status & 0x60) == 0x20) {
    return cursor;
  }
  cursor += kControlParameters[status & 0x1f];
  if ((status & 0x60) == 0x40) {
    const auto next = skipVariable(reader, cursor, end);
    if (!next) return std::nullopt;
    cursor = *next;
  } else if ((status & 0x60) == 0x60) {
    ++cursor;
  }
  return cursor <= end ? std::optional(cursor) : std::nullopt;
}

[[nodiscard]] std::optional<u32> trackEnd(ByteReader reader, u32 start, u32 limit) {
  u32 cursor = start;
  for (u32 commands = 0; commands < 262144 && cursor < limit;) {
    const u8 status = reader.u8At(cursor);
    const bool end = status >= 0x80 && (status & 0x60) != 0x20 && (status & 0x1f) == 0;
    const auto next = eventEnd(reader, cursor, limit);
    if (!next || *next <= cursor) {
      return std::nullopt;
    }
    cursor = *next;
    ++commands;
    if (end) {
      return cursor;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool validBank(ByteReader reader, u32 bank, u32 bankEnd, OhoriAkaPs1BankLayout& layout) {
  if (!rangeValid(reader, bank, 12) || bankEnd <= bank + 12 || reader.le32(bank) != 0 || reader.le32(bank + 4) != 0) {
    return false;
  }
  const u32 count = reader.le32(bank + 8);
  if (count == 0 || count > kMaximumInstruments || !rangeValid(reader, bank + 12, count * 4ull)) {
    return false;
  }
  const u32 tableEnd = 12 + count * 4;
  layout.instrumentAddresses.clear();
  layout.instrumentAddresses.reserve(count);
  u32 parsedEnd = tableEnd;
  for (u32 i = 0; i < count; ++i) {
    const u32 relative = reader.le32(bank + 12 + i * 4);
    if (relative < tableEnd || relative >= bankEnd - bank || !reader.has(bank + relative, 4)) {
      return false;
    }
    const u32 regions = reader.le32(bank + relative);
    const u64 end = static_cast<u64>(relative) + 4 + static_cast<u64>(regions) * kRegionSize;
    if (regions == 0 || regions > 128 || end > bankEnd - bank) {
      return false;
    }
    layout.instrumentAddresses.push_back(bank + relative);
    parsedEnd = std::max(parsedEnd, static_cast<u32>(end));
  }
  layout.offset = bank;
  layout.length = parsedEnd;
  layout.instrumentCount = count;
  return true;
}

[[nodiscard]] bool plausibleAdpcm(ByteReader reader, u32 offset, u32 boundary) {
  if (boundary <= offset || ((boundary - offset) & 15) != 0 || !rangeValid(reader, offset, boundary - offset)) {
    return false;
  }
  bool ended = false;
  for (u32 cursor = offset; cursor < boundary; cursor += kPsxAdpcmBlockBytes) {
    const u8 filterShift = reader.u8At(cursor);
    const u8 flags = reader.u8At(cursor + 1);
    if ((filterShift >> 4) > 4 || (filterShift & 0x0f) > 12 || (flags & ~u8{7}) != 0) {
      return false;
    }
    if ((flags & 1) != 0) {
      ended = true;
      break;
    }
  }
  return ended;
}

void locateSamples(ByteReader reader, OhoriAkaPs1BankLayout& layout) {
  std::set<u32> offsets;
  for (const u32 instrument : layout.instrumentAddresses) {
    const u32 count = reader.le32(instrument);
    for (u32 region = 0; region < count; ++region) {
      offsets.insert(reader.le32(instrument + 4 + region * kRegionSize));
    }
  }
  if (offsets.size() < 2) {
    return;
  }
  const std::vector<u32> samples(offsets.begin(), offsets.end());
  const u32 maximum = samples.back();
  // The PSF-derived RAM source need not begin at a 16-byte address, so native
  // ADPCM alignment can appear at any four-byte residue in the ByteReader.
  for (u64 base = 0; base + maximum + kPsxAdpcmBlockBytes <= reader.size(); base += 4) {
    const auto firstBlock = reader.slice(base + samples.front(), kPsxAdpcmBlockBytes);
    if (std::ranges::all_of(firstBlock, [](u8 byte) { return byte == 0; }) ||
        !plausibleAdpcm(reader, static_cast<u32>(base + samples[0]), static_cast<u32>(base + samples[1]))) {
      continue;
    }
    u32 matches = 1;
    for (std::size_t i = 1; i + 1 < samples.size() && matches < 6; ++i) {
      if (plausibleAdpcm(reader, static_cast<u32>(base + samples[i]), static_cast<u32>(base + samples[i + 1]))) {
        ++matches;
      } else {
        break;
      }
    }
    if (matches >= std::min<std::size_t>(4, samples.size() - 1)) {
      layout.sampleDataOffset = static_cast<u32>(base);
      // The last stream supplies its own terminator, so the remaining source
      // is only an upper bound and does not become part of the sample.
      layout.sampleDataLength = static_cast<u32>(reader.size() - base);
      return;
    }
  }
}

}  // namespace

std::optional<OhoriAkaPs1SequenceLayout> readOhoriAkaPs1SequenceLayout(ByteReader reader, u32 offset) {
  if (!rangeValid(reader, offset, kSequenceHeaderSize) || reader.le32(offset) != kSequenceSignature ||
      reader.u8At(offset + 4) != 'V') {
    return std::nullopt;
  }
  OhoriAkaPs1SequenceLayout layout{
      .offset = offset,
      .version = reader.u8At(offset + 5),
      .trackCount = reader.u8At(offset + 6),
      .reverbMode = reader.u8At(offset + 7),
      .reverbDepth = reader.le16(offset + 8),
      .reverbDelay = reader.u8At(offset + 10),
      .reverbFeedback = reader.u8At(offset + 11),
      .leftGain = reader.le16(offset + 12),
      .rightGain = reader.le16(offset + 14),
  };
  if (layout.trackCount == 0 || layout.trackCount > kMaximumTracks ||
      !rangeValid(reader, offset + kSequenceHeaderSize, layout.trackCount * 2ull)) {
    return std::nullopt;
  }
  layout.durations.reserve(32);
  for (u32 i = 0; i < 32; ++i) {
    layout.durations.push_back(reader.le16(offset + 0x10 + i * 2));
  }
  layout.trackAddresses.reserve(layout.trackCount);
  for (u32 i = 0; i < layout.trackCount; ++i) {
    const u16 relative = reader.le16(offset + kSequenceHeaderSize + i * 2);
    if (relative < kSequenceHeaderSize + layout.trackCount * 2 ||
        (i != 0 && relative < layout.trackAddresses.back() - offset) || !reader.has(offset + relative, 1)) {
      return std::nullopt;
    }
    layout.trackAddresses.push_back(offset + relative);
  }
  layout.trackEnds.reserve(layout.trackCount);
  u32 sequenceEnd = offset + kSequenceHeaderSize + layout.trackCount * 2;
  for (u32 i = 0; i < layout.trackCount; ++i) {
    const u32 limit = i + 1 < layout.trackCount
                          ? layout.trackAddresses[i + 1]
                          : static_cast<u32>(std::min<u64>(reader.size(), offset + kMaximumSequenceSize));
    const auto end = trackEnd(reader, layout.trackAddresses[i], limit);
    if (!end) {
      return std::nullopt;
    }
    layout.trackEnds.push_back(*end);
    sequenceEnd = std::max(sequenceEnd, *end);
  }
  layout.length = sequenceEnd - offset;
  return layout;
}

std::vector<OhoriAkaPs1SequenceLayout> findOhoriAkaPs1Sequences(ByteReader reader) {
  std::vector<OhoriAkaPs1SequenceLayout> layouts;
  for (u64 offset = 0; offset + kSequenceHeaderSize <= reader.size(); ++offset) {
    if (reader.le32(offset) != kSequenceSignature || offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    if (auto layout = readOhoriAkaPs1SequenceLayout(reader, static_cast<u32>(offset))) {
      layouts.push_back(std::move(*layout));
      offset += layouts.back().length - 1;
    }
  }
  return layouts;
}

std::optional<OhoriAkaPs1BankLayout> findOhoriAkaPs1Bank(ByteReader reader,
                                                         const OhoriAkaPs1SequenceLayout& sequence) {
  // Threads of Fate packages a zero-based bank at container+0x10 and stores
  // the HOSAV offset at container+0x08. Searching that explicit back-pointer
  // is both safer and cheaper than the legacy sample-offset guess.
  const u32 searchBegin = sequence.offset > 0x10000 ? sequence.offset - 0x10000 : 0;
  for (u32 container = sequence.offset & ~u32{3};; container -= 4) {
    if (!reader.has(container, 16) || reader.le32(container) != 0 || reader.le32(container + 4) != 0 ||
        reader.le32(container + 8) != sequence.offset - container) {
      if (container < searchBegin + 4) break;
      continue;
    }
    OhoriAkaPs1BankLayout layout{.containerOffset = container};
    if (validBank(reader, container + 0x10, sequence.offset, layout)) {
      locateSamples(reader, layout);
      return layout;
    }
    if (container < searchBegin + 4) break;
  }
  return std::nullopt;
}

}  // namespace vgmtrans::formats::ohori_aka_ps1

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OhoriAkaPS1/OhoriAkaPS1.h"
#include "value/formats/OhoriAkaPS1/OhoriAkaPS1Bytecode.h"

#include "value/synth/PsxAdpcm.h"

#include <algorithm>
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

[[nodiscard]] bool zeroFilled(ByteReader reader, u32 offset, u32 size) {
  return reader.has(offset, size) &&
         std::ranges::all_of(reader.slice(offset, size), [](u8 byte) { return byte == 0; });
}

[[nodiscard]] std::optional<u32> trackEnd(ByteReader reader, u32 start, u32 limit, bool finalTrack) {
  u32 cursor = start;
  for (u32 commands = 0; commands < bytecode::kMaximumCommands && cursor < limit; ++commands) {
    // Some HOSAV streams omit the final 0x80 and end directly at the
    // container's zero-filled tail. A single 00 00 remains a valid key-zero
    // note; only a sustained run at an event boundary terminates the last
    // track.
    if (finalTrack && limit - cursor >= kPsxAdpcmBlockBytes && zeroFilled(reader, cursor, kPsxAdpcmBlockBytes)) {
      return cursor;
    }
    const u8 status = reader.u8At(cursor);
    const auto next = bytecode::commandEnd(reader, cursor, limit);
    if (!next || *next <= cursor) {
      return std::nullopt;
    }
    cursor = *next;
    if (bytecode::isControl(status, 0)) {
      return cursor;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<OhoriAkaPs1BankLayout> readBank(ByteReader reader, u32 bank, u32 bankEnd) {
  if (!reader.has(bank, 12) || bankEnd <= bank + 12 || reader.le32(bank) != 0 || reader.le32(bank + 4) != 0) {
    return std::nullopt;
  }
  const u32 count = reader.le32(bank + 8);
  if (count == 0 || count > kMaximumInstruments || !reader.has(bank + 12, count * 4ull)) {
    return std::nullopt;
  }
  const u32 tableEnd = 12 + count * 4;
  OhoriAkaPs1BankLayout layout{.offset = bank};
  layout.instrumentAddresses.reserve(count);
  u32 parsedEnd = tableEnd;
  for (u32 i = 0; i < count; ++i) {
    const u32 relative = reader.le32(bank + 12 + i * 4);
    if (relative < tableEnd || relative >= bankEnd - bank || !reader.has(bank + relative, 4)) {
      return std::nullopt;
    }
    const u32 regions = reader.le32(bank + relative);
    const u64 end = static_cast<u64>(relative) + 4 + static_cast<u64>(regions) * kRegionSize;
    if (regions == 0 || regions > 128 || end > bankEnd - bank) {
      return std::nullopt;
    }
    layout.instrumentAddresses.push_back(bank + relative);
    parsedEnd = std::max(parsedEnd, static_cast<u32>(end));
  }
  layout.length = parsedEnd;
  return layout;
}

[[nodiscard]] bool validAdpcmBlock(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kPsxAdpcmBlockBytes)) return false;
  const u8 filterShift = reader.u8At(offset);
  const u8 flags = reader.u8At(offset + 1);
  return (filterShift >> 4) <= 4 && (filterShift & 0x0f) <= 12 && (flags & ~u8{7}) == 0;
}

[[nodiscard]] bool terminatedAdpcm(ByteReader reader, u32 offset, u32 boundary) {
  if (boundary <= offset || (boundary - offset) % kPsxAdpcmBlockBytes != 0 ||
      !reader.has(offset, boundary - offset)) {
    return false;
  }
  for (u32 cursor = offset; cursor < boundary; cursor += kPsxAdpcmBlockBytes) {
    if (!validAdpcmBlock(reader, cursor)) return false;
    if ((reader.u8At(cursor + 1) & 1) != 0) return true;
  }
  return false;
}

[[nodiscard]] bool samplePoolStart(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kPsxAdpcmBlockBytes * 2)) return false;
  const u32 first = offset + kPsxAdpcmBlockBytes;
  return zeroFilled(reader, offset, kPsxAdpcmBlockBytes) && reader.le16(first) != 0 && validAdpcmBlock(reader, first);
}

[[nodiscard]] std::optional<u32> locateSamples(ByteReader reader, const std::vector<u32>& instruments) {
  std::set<u32> offsets;
  for (const u32 instrument : instruments) {
    const u32 count = reader.le32(instrument);
    for (u32 region = 0; region < count; ++region) {
      offsets.insert(reader.le32(instrument + 4 + region * kRegionSize));
    }
  }
  if (offsets.size() < 2) return std::nullopt;
  const std::vector<u32> samples(offsets.begin(), offsets.end());
  const u32 maximum = samples.back();
  u32 bestBase = 0;
  u32 bestMatches = 0;
  // The PSF-derived RAM source need not begin at a 16-byte address, so native
  // ADPCM alignment can appear at any four-byte residue in the ByteReader.
  for (u64 base = 0; base + maximum + kPsxAdpcmBlockBytes <= reader.size(); base += 4) {
    if (!samplePoolStart(reader, static_cast<u32>(base))) continue;
    u32 matches = 0;
    for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
      if (terminatedAdpcm(reader, static_cast<u32>(base + samples[i]), static_cast<u32>(base + samples[i + 1]))) {
        ++matches;
      } else {
        break;
      }
    }
    if (matches > bestMatches) {
      bestBase = static_cast<u32>(base);
      bestMatches = matches;
    }
  }
  return bestMatches >= std::min<std::size_t>(4, samples.size() - 1)
             ? std::optional<u32>{bestBase}
             : std::nullopt;
}

}  // namespace

std::optional<OhoriAkaPs1SequenceLayout> readOhoriAkaPs1SequenceLayout(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kSequenceHeaderSize) || reader.le32(offset) != kSequenceSignature ||
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
      !reader.has(offset + kSequenceHeaderSize, layout.trackCount * 2ull)) {
    return std::nullopt;
  }
  for (u32 i = 0; i < layout.durations.size(); ++i) {
    layout.durations[i] = reader.le16(offset + 0x10 + i * 2);
  }
  layout.tracks.reserve(layout.trackCount);
  for (u32 i = 0; i < layout.trackCount; ++i) {
    const u16 relative = reader.le16(offset + kSequenceHeaderSize + i * 2);
    if (relative < kSequenceHeaderSize + layout.trackCount * 2 ||
        (i != 0 && relative < layout.tracks.back().offset - offset) || !reader.has(offset + relative, 1)) {
      return std::nullopt;
    }
    layout.tracks.push_back(OhoriAkaPs1TrackLayout{.offset = offset + relative});
  }
  u32 sequenceEnd = offset + kSequenceHeaderSize + layout.trackCount * 2;
  for (u32 i = 0; i < layout.trackCount; ++i) {
    const u32 limit = i + 1 < layout.tracks.size()
                          ? layout.tracks[i + 1].offset
                          : static_cast<u32>(std::min<u64>(reader.size(), offset + kMaximumSequenceSize));
    const auto end = trackEnd(reader, layout.tracks[i].offset, limit, i + 1 == layout.tracks.size());
    if (!end) {
      return std::nullopt;
    }
    layout.tracks[i].end = *end;
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
    if (auto layout = readBank(reader, container + 0x10, sequence.offset)) {
      layout->sampleDataOffset = locateSamples(reader, layout->instrumentAddresses);
      return layout;
    }
    if (container < searchBegin + 4) break;
  }
  return std::nullopt;
}

}  // namespace vgmtrans::formats::ohori_aka_ps1

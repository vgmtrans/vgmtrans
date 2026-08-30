/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HOSA/HOSA.h"
#include "value/formats/HOSA/HOSABytecode.h"

#include "value/synth/PsxAdpcm.h"

#include <algorithm>
#include <limits>

namespace vgmtrans::formats::hosa {

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

[[nodiscard]] std::optional<u32> trackEnd(ByteReader reader, u32 start, u32 limit) {
  u32 cursor = start;
  for (u32 commands = 0; commands < bytecode::kMaximumCommands && cursor < limit; ++commands) {
    // Several game streams omit 0x80 and end in the resource's zero tail.
    // A full ADPCM-sized run distinguishes padding from an isolated key-zero note.
    if (limit - cursor >= kPsxAdpcmBlockBytes && zeroFilled(reader, cursor, kPsxAdpcmBlockBytes)) {
      return cursor;
    }
    const u8 status = reader.u8At(cursor);
    const auto next = bytecode::commandEnd(reader, cursor, limit);
    if (!next || *next <= cursor) return std::nullopt;
    cursor = *next;
    if (bytecode::isControl(status, 0)) return cursor;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BankLayout> readBank(ByteReader reader, u32 bank, u32 bankEnd) {
  if (!reader.has(bank, 12) || bankEnd <= bank + 12 || reader.le32(bank) != 0 || reader.le32(bank + 4) != 0) {
    return std::nullopt;
  }
  const u32 count = reader.le32(bank + 8);
  if (count == 0 || count > kMaximumInstruments || !reader.has(bank + 12, count * 4ull)) {
    return std::nullopt;
  }

  const u32 tableEnd = 12 + count * 4;
  BankLayout layout{.offset = bank};
  layout.instrumentAddresses.reserve(count);
  u32 parsedEnd = tableEnd;
  for (u32 i = 0; i < count; ++i) {
    const u32 relative = reader.le32(bank + 12 + i * 4);
    if (relative < tableEnd || relative >= bankEnd - bank || !reader.has(bank + relative, 4)) {
      return std::nullopt;
    }
    const u32 regions = reader.u8At(bank + relative);
    const u64 end = static_cast<u64>(relative) + 4 + static_cast<u64>(regions) * kRegionSize;
    if (regions == 0 || end > bankEnd - bank) return std::nullopt;
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

[[nodiscard]] bool terminatedAdpcm(ByteReader reader, u32 offset) {
  for (u32 cursor = offset; reader.has(cursor, kPsxAdpcmBlockBytes); cursor += kPsxAdpcmBlockBytes) {
    if (!validAdpcmBlock(reader, cursor)) return false;
    if ((reader.u8At(cursor + 1) & 1) != 0) return true;
  }
  return false;
}

[[nodiscard]] bool samplePoolStart(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kPsxAdpcmBlockBytes * 2)) return false;
  const u32 first = offset + kPsxAdpcmBlockBytes;
  return zeroFilled(reader, offset, kPsxAdpcmBlockBytes) && !zeroFilled(reader, first, kPsxAdpcmBlockBytes) &&
         validAdpcmBlock(reader, first);
}

[[nodiscard]] u32 matchingSamples(ByteReader reader, u64 base, const std::vector<u32>& samples) {
  u32 matches = 0;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const u32 start = static_cast<u32>(base + samples[i]);
    const bool valid = i + 1 == samples.size()
                           ? terminatedAdpcm(reader, start)
                           : terminatedAdpcm(reader, start, static_cast<u32>(base + samples[i + 1]));
    if (!valid) {
      break;
    }
    ++matches;
  }
  return matches;
}

[[nodiscard]] std::optional<u32> locateSamples(ByteReader reader, const std::vector<u32>& instruments) {
  std::vector<u32> samples;
  for (const u32 instrument : instruments) {
    const u32 count = reader.u8At(instrument);
    for (u32 region = 0; region < count; ++region) {
      samples.push_back(reader.le32(instrument + 4 + region * kRegionSize));
    }
  }
  std::ranges::sort(samples);
  samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
  if (samples.empty()) return std::nullopt;

  const u32 maximum = samples.back();
  const u32 perfect = static_cast<u32>(samples.size());
  // PSF RAM sources can place an SPU transfer buffer at any four-byte residue.
  for (u64 base = 0; base + maximum + kPsxAdpcmBlockBytes <= reader.size(); base += 4) {
    if (!samplePoolStart(reader, static_cast<u32>(base))) continue;
    if (matchingSamples(reader, base, samples) == perfect) return static_cast<u32>(base);
  }
  return std::nullopt;
}

}  // namespace

std::optional<SequenceLayout> readSequenceLayout(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kSequenceHeaderSize) || reader.le32(offset) != kSequenceSignature ||
      reader.u8At(offset + 4) != 'V') {
    return std::nullopt;
  }

  const u8 trackCount = reader.u8At(offset + 6);
  if (trackCount == 0 || trackCount > kMaximumTracks ||
      !reader.has(offset + kSequenceHeaderSize, trackCount * 2ull)) {
    return std::nullopt;
  }
  SequenceLayout layout{
      .offset = offset,
      .version = reader.u8At(offset + 5),
      .reverb =
          ReverbConfig{
              .mode = reader.u8At(offset + 7),
              .depth = static_cast<s16>(reader.le16(offset + 8)),
              .delay = reader.u8At(offset + 10),
              .feedback = reader.u8At(offset + 11),
          },
      .leftGain = static_cast<s16>(reader.le16(offset + 12)),
      .rightGain = static_cast<s16>(reader.le16(offset + 14)),
  };
  for (u32 i = 0; i < layout.durations.size(); ++i) {
    layout.durations[i] = reader.le16(offset + 0x10 + i * 2);
  }

  layout.tracks.reserve(trackCount);
  for (u32 i = 0; i < trackCount; ++i) {
    const u16 relative = reader.le16(offset + kSequenceHeaderSize + i * 2);
    if (relative < kSequenceHeaderSize + trackCount * 2 || !reader.has(offset + relative, 1)) {
      return std::nullopt;
    }
    layout.tracks.push_back(TrackLayout{.offset = offset + relative});
  }

  // Track pointers are starts, not bounds: the driver permits streams to join
  // a later track and share its tail. Decode each one against the common resource limit.
  const u32 limit = static_cast<u32>(std::min<u64>(reader.size(), offset + kMaximumSequenceSize));
  u32 sequenceEnd = offset + kSequenceHeaderSize + trackCount * 2;
  for (auto& track : layout.tracks) {
    const auto end = trackEnd(reader, track.offset, limit);
    if (!end) return std::nullopt;
    track.end = *end;
    sequenceEnd = std::max(sequenceEnd, *end);
  }
  layout.length = sequenceEnd - offset;
  return layout;
}

std::vector<SequenceLayout> findSequences(ByteReader reader) {
  std::vector<SequenceLayout> layouts;
  for (u64 offset = 0; offset + kSequenceHeaderSize <= reader.size(); ++offset) {
    if (reader.le32(offset) != kSequenceSignature || offset > std::numeric_limits<u32>::max()) continue;
    if (auto layout = readSequenceLayout(reader, static_cast<u32>(offset))) {
      layouts.push_back(std::move(*layout));
      offset += layouts.back().length - 1;
    }
  }
  return layouts;
}

std::optional<BankLayout> findBank(ByteReader reader, const SequenceLayout& sequence) {
  // The resource container stores the HOSAV relative address at +8 and its
  // zero-based instrument bank at +0x10.
  for (u32 container = sequence.offset & ~u32{3};; container -= 4) {
    if (reader.has(container, 16) && reader.le32(container) == 0 && reader.le32(container + 4) == 0 &&
        reader.le32(container + 8) == sequence.offset - container) {
      if (auto layout = readBank(reader, container + 0x10, sequence.offset)) {
        layout->sampleDataOffset = locateSamples(reader, layout->instrumentAddresses);
        return layout;
      }
    }
    if (container < 4) break;
  }
  return std::nullopt;
}

}  // namespace vgmtrans::formats::hosa

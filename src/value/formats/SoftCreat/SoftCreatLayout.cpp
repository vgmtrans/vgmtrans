/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SoftCreat/SoftCreat.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <bit>
#include <optional>
#include <string_view>

namespace vgmtrans::formats::softcreat {

using namespace core;
using namespace std::string_view_literals;

namespace {

// All five drivers select one byte from each of sixteen parallel low/high
// pointer columns. The two table operands reveal both the song count (their
// distance) and the start of the complete eight-track header.
constexpr auto kLoadSequence = makeMaskedBytePattern(
    "\x7d\x68\x05\xb0\xfa\xfd\xcd\x00\xf6\x85\x13\xf0\x0a\xc4\x31\xf6\x80\x13\xc4\x30\x3f\x38\x06\x3d\x3d",
    "xx?x?xxxx??x?x?x??x?x??xx");

constexpr auto kDispatch = makeMaskedBytePattern(
    "\x3f\x85\x07\x10\x1d\x68\xba\xb0\x0b\x1c\xfd\xf6\x70\x0b\x2d\xf6\x6f\x0b\x2d\x6f",
    "x??x?x?xxxxx??xx??xx");

constexpr auto kCoarseTable = makeMaskedBytePattern(
    "\xfb\x20\x60\x96\x90\x42\x5b\xc0\xb0\x04\x60\x95\x40\x02\xd5\xb0\x02\xd5\x90\x02",
    "xxxx??xxxxxxxxxxxxxx");

constexpr auto kPitchAndFineTables = makeMaskedBytePattern(
    "\xf6\x87\x12\xc4\xd9\xf6\xdc\x12\xc4\xda\xfb\x20\xf6\xb4\x42\xfd\x6d\xe4\xd9\xcf\xcb\xdd\xee\xe4\xda\xcf\x8f\x00\xde\x7a\xdd\x7a\xd9",
    "x??xxx??xxxxx??xxxxxxxxxxxxxxxxxx");

constexpr auto kEnvelopeTable = makeMaskedBytePattern(
    "\xe8\x3e\xc4\xd9\xe8\x1e\xc4\xda", "x?xxx?xx");

constexpr auto kTimerSetup = makeMaskedBytePattern("\x8f\x84\xfc\x8f\x04\xf1", "x?xxxx");

constexpr std::array<u8, 27> kDspRegisters{
    0x2c, 0x3c, 0x5c, 0x2d, 0x3d, 0x4d, 0x7d, 0x6d, 0x0d, 0x5d, 0x0f, 0x1f, 0x2f, 0x3f,
    0x4f, 0x5f, 0x6f, 0x7f, 0x05, 0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75, 0xff,
};

constexpr std::array kVersions{
    Version::Early,
    Version::Plok,
    Version::MaximumCarnage,
    Version::LateEcho,
    Version::LateNoEcho,
};

[[nodiscard]] std::optional<Version> versionForCutoff(u8 cutoff) {
  const auto found = std::ranges::find_if(kVersions, [cutoff](Version version) {
    return dialect(version).commandCutoff == cutoff;
  });
  return found == kVersions.end() ? std::nullopt : std::optional<Version>{*found};
}

[[nodiscard]] std::optional<u32> findBytes(ByteReader reader, std::span<const u8> bytes) {
  if (bytes.empty() || bytes.size() > reader.size()) {
    return std::nullopt;
  }
  for (u32 offset = 0; offset <= reader.size() - bytes.size(); ++offset) {
    if (matchesBytes(reader, offset, bytes)) {
      return offset;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool validTrackPointer(ByteReader reader, u16 address) {
  return address >= 0x0200 && reader.has(address, 1);
}

[[nodiscard]] u8 bestSong(ByteReader reader, u16 list, u8 songs, u8 live) {
  const auto score = [&](u8 song) {
    unsigned active = 0;
    for (u32 track = 0; track < kTrackCount; ++track) {
      const u32 low = list + track * songs * 2u + song;
      if (!reader.has(low, songs + 1)) {
        continue;
      }
      const u16 address = static_cast<u16>(reader.u8At(low) | (reader.u8At(low + songs) << 8));
      active += validTrackPointer(reader, address);
    }
    return active;
  };

  if (live < songs && score(live) != 0) {
    return live;
  }
  u8 selected = 0;
  unsigned selectedScore = 0;
  for (u8 song = 0; song < songs; ++song) {
    if (const unsigned candidate = score(song); candidate > selectedScore) {
      selected = song;
      selectedScore = candidate;
    }
  }
  return selected;
}

[[nodiscard]] std::optional<u16> aliasTable(ByteReader reader, Version version, u16 dispatch) {
  const auto opcode = dialect(version).noteAliasOpcode;
  if (!opcode) {
    return std::nullopt;
  }
  const u32 entry = dispatch + (*opcode - 0x80u) * 2u;
  if (!reader.has(entry, 2)) {
    return std::nullopt;
  }
  const u16 handler = reader.le16(entry);
  // call read-byte; mov y,a; mov a,table+y
  if (!reader.has(handler, 8) || reader.u8At(handler) != 0x3f || reader.u8At(handler + 3) != 0xfd ||
      reader.u8At(handler + 4) != 0xf6) {
    return std::nullopt;
  }
  const u16 table = reader.le16(handler + 5);
  return reader.has(table, 16) ? std::optional<u16>{table} : std::nullopt;
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto load = findBytePattern(reader, kLoadSequence);
  const auto dispatchCode = findBytePattern(reader, kDispatch);
  const auto coarseCode = findBytePattern(reader, kCoarseTable);
  const auto pitchCode = findBytePattern(reader, kPitchAndFineTables);
  const auto envelopeCode = findBytePattern(reader, kEnvelopeTable);
  const auto dspRegisters = findBytes(reader, kDspRegisters);
  if (!load || !dispatchCode || !coarseCode || !pitchCode || !envelopeCode || !dspRegisters) {
    return std::nullopt;
  }

  const u8 cutoff = reader.u8At(*dispatchCode + 6);
  const auto version = versionForCutoff(cutoff);
  const u16 list = reader.le16(*load + 16);
  const u16 highTable = reader.le16(*load + 9);
  const u16 dispatch = reader.le16(*dispatchCode + 16);
  if (!version || highTable <= list || highTable - list > 8 || !reader.has(dispatch, (cutoff - 0x80u) * 2u) ||
      reader.u8At(*load + 2) < highTable - list) {
    return std::nullopt;
  }
  const u8 songs = static_cast<u8>(highTable - list);
  if (!reader.has(list, kTrackCount * songs * 2u)) {
    return std::nullopt;
  }

  const u8 song = bestSong(reader, list, songs, reader.u8At(0xe4));
  std::array<TrackPointer, kTrackCount> tracks{};
  unsigned active = 0;
  u16 firstTrack = 0xffff;
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u32 low = list + track * songs * 2u + song;
    const u32 high = low + songs;
    const u16 address = static_cast<u16>(reader.u8At(low) | (reader.u8At(high) << 8));
    tracks[track] = TrackPointer{
        .address = address,
        .lowSource = reader.range(low, 1),
        .highSource = reader.range(high, 1),
    };
    if (validTrackPointer(reader, address)) {
      ++active;
      firstTrack = std::min(firstTrack, address);
    } else if (address != 0) {
      return std::nullopt;
    }
  }
  if (active == 0) {
    return std::nullopt;
  }

  const u16 pitchLow = reader.le16(*pitchCode + 1);
  const u16 pitchHigh = reader.le16(*pitchCode + 6);
  const u16 coarse = reader.le16(*coarseCode + 4);
  const u16 fine = reader.le16(*pitchCode + 13);
  const u16 envelopes = static_cast<u16>(reader.u8At(*envelopeCode + 1) | (reader.u8At(*envelopeCode + 5) << 8));
  const u32 dspValues = *dspRegisters + kDspRegisters.size();
  if (pitchHigh <= pitchLow) {
    return std::nullopt;
  }
  const u32 pitchCount = pitchHigh - pitchLow;
  if (!reader.has(pitchLow, pitchCount) || !reader.has(pitchHigh, pitchCount) || !reader.has(coarse, 1) ||
      !reader.has(fine, 1) || !reader.has(envelopes, 7) || !reader.has(dspValues, 26)) {
    return std::nullopt;
  }

  EchoState echo{
      .left = reader.s8At(dspValues),
      .right = reader.s8At(dspValues + 1),
      .feedback = reader.s8At(dspValues + 8),
      .voiceMask = reader.u8At(dspValues + 5),
      .delay = static_cast<u8>(reader.u8At(dspValues + 6) & 0x0f),
  };
  for (u32 coefficient = 0; coefficient < echo.fir.size(); ++coefficient) {
    echo.fir[coefficient] = reader.s8At(dspValues + 10 + coefficient);
  }
  const u16 directory = static_cast<u16>(reader.u8At(dspValues + 9) << 8);
  if (!reader.has(directory, 4)) {
    return std::nullopt;
  }

  u8 timer = 0x85;
  if (const auto setup = findBytePattern(reader, kTimerSetup)) {
    timer = reader.u8At(*setup + 1);
  }
  return Layout{
      .version = *version,
      .songIndex = song,
      .songCount = songs,
      .initialTimer = timer,
      .musicVolume = reader.u8At(0xe8),
      .pitchLowTableAddress = pitchLow,
      .pitchHighTableAddress = pitchHigh,
      .coarseTableAddress = coarse,
      .fineTableAddress = fine,
      .envelopeTableAddress = envelopes,
      .spcDirAddress = directory,
      .noteAliasTableAddress = aliasTable(reader, *version, dispatch),
      .sequenceHeaderRange = reader.range(list, kTrackCount * songs * 2u),
      .tracks = tracks,
      .echo = echo,
  };
}

}  // namespace vgmtrans::formats::softcreat

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PrismSnes/PrismSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace vgmtrans::formats::prism_snes {

using namespace core;

namespace {

constexpr auto kLoadSequence =
    makeMaskedBytePattern("\xf6\x00\x23\xc4\x06\xfc\xf6\x00\x23\xc4\x07\x8d\x00\xf7\x06\x30\xd4", "x??x?xx??x?xxx?x?");
constexpr auto kExecuteCommand = makeMaskedBytePattern(
    "\x8d\x00\xf7\x26\x10\x1f\x3a\x26\x68\xa0\x90\x08\x80\xa8\xc0\x1c\x5d\x1f\xff\x0e", "xxx?x?x?xxxxxxxxxx??");
constexpr auto kSetDspRegisters =
    makeMaskedBytePattern("\x8d\x16\xe8\x18\xda\x00\x8d\x07\xcd\x7d\x3f\x05\x16", "x?x?x?xxxxx??");
constexpr auto kLoadInstrument = makeMaskedBytePattern(
    "\xf8\x24\xf7\x26\x3a\x26\xd5\x50\x03\xfd\xf6\x00\x1f\xd5\xb0\x03\xf6\x00\x20\xd5\xc8\x03\x38\x7f\x28\x8f\xff\x39",
    "x?x?x?x??xx??x??x??x??xx?xx?");
constexpr auto kLoadTuning =
    makeMaskedBytePattern("\xf8\x42\xf5\x00\x21\x60\x84\x2a\xc4\x2a\xf5\x00\x22\x60\x84\x29\xc4\x29\x90\x02\xab\x2a",
                          "x?x??xx?x?x??xx?x?xxx?");

constexpr std::array<std::array<s8, 8>, 4> kFirPresets{{
    {{0x7f, 0, 0, 0, 0, 0, 0, 0}},
    {{0x58, -0x41, -0x25, -0x10, -2, 7, 0x0c, 0x0c}},
    {{0x0c, 0x21, 0x2b, 0x2b, 0x13, -2, -0x0d, -7}},
    {{0x34, 0x33, 0, -0x27, -0x1b, 1, -4, -0x15}},
}};

struct SongCandidate {
  u8 index = 0;
  u16 header = 0;
  std::vector<TrackHeader> tracks;
  u32 liveMatches = 0;
  u32 liveDistance = 0;
};

[[nodiscard]] std::optional<std::vector<TrackHeader>> readHeader(ByteReader reader, u16 address) {
  std::vector<TrackHeader> tracks;
  std::set<u8> logicalChannels;
  u32 cursor = address;
  for (u32 index = 0; index < kMaxTracks; ++index, cursor += 4) {
    if (!reader.has(cursor, 1)) {
      return std::nullopt;
    }
    const u8 logical = reader.u8At(cursor);
    if (logical >= 0x80) {
      return tracks.empty() ? std::nullopt : std::optional{tracks};
    }
    if (logical >= kMaxTracks || logicalChannels.contains(logical) || !reader.has(cursor, 4)) {
      return std::nullopt;
    }
    const u16 start = reader.le16(cursor + 2);
    if (start == 0 || !reader.has(start, 1)) {
      return std::nullopt;
    }
    logicalChannels.insert(logical);
    tracks.push_back(TrackHeader{
        .logicalChannel = logical,
        .physicalChannelFlags = reader.u8At(cursor + 1),
        .startAddress = start,
        .range = reader.range(cursor, 4),
    });
  }
  return std::nullopt;
}

void scoreLiveState(ByteReader reader, SongCandidate& song) {
  for (const TrackHeader& track : song.tracks) {
    const u32 logical = track.logicalChannel;
    const u8 state = reader.u8At(0x0200 + logical);
    const u16 current = static_cast<u16>(reader.u8At(0x0230 + logical) | (reader.u8At(0x0248 + logical) << 8));
    if ((state & 0x80) == 0 || (state & 7) != (track.physicalChannelFlags & 7) || current < track.startAddress) {
      continue;
    }
    const u32 distance = current - track.startAddress;
    // Active pointers can be inside calls, but false headers found in driver
    // data tend to be tens of kilobytes away from every live score pointer.
    if (distance <= 0x4000) {
      ++song.liveMatches;
      song.liveDistance += distance;
    }
  }
}

[[nodiscard]] std::optional<SongCandidate> selectSong(ByteReader reader, u16 listAddress) {
  std::vector<SongCandidate> songs;
  for (u32 index = 0; index < 128 && reader.has(listAddress + index * 2, 2); ++index) {
    const u16 header = reader.le16(listAddress + index * 2);
    if (header == 0) {
      break;
    }
    const auto tracks = readHeader(reader, header);
    if (!tracks) {
      break;
    }
    SongCandidate song{.index = static_cast<u8>(index), .header = header, .tracks = *tracks};
    scoreLiveState(reader, song);
    songs.push_back(std::move(song));
  }
  if (songs.empty()) {
    return std::nullopt;
  }
  return *std::ranges::max_element(songs, [](const SongCandidate& left, const SongCandidate& right) {
    if (left.liveMatches != right.liveMatches) {
      return left.liveMatches < right.liveMatches;
    }
    if (left.liveDistance != right.liveDistance) {
      return left.liveDistance > right.liveDistance;
    }
    return left.index > right.index;
  });
}

[[nodiscard]] std::optional<u16> panTableFromHandler(ByteReader reader, u16 commandTable, u8 command) {
  const u16 handler = reader.le16(commandTable + static_cast<u32>(command - 0xc0) * 2);
  if (!reader.has(handler, 12) || reader.u8At(handler) != 0xf8 || reader.u8At(handler + 2) != 0xe8 ||
      reader.u8At(handler + 4) != 0xd5 || reader.u8At(handler + 7) != 0xe8 || reader.u8At(handler + 9) != 0xd5) {
    return std::nullopt;
  }
  const u16 address = static_cast<u16>(reader.u8At(handler + 3) | (reader.u8At(handler + 8) << 8));
  return reader.has(address, 21) ? std::optional{address} : std::nullopt;
}

[[nodiscard]] u8 firPreset(ByteReader reader, u16 address) {
  for (u8 preset = 0; preset < kFirPresets.size(); ++preset) {
    bool matches = reader.has(address, 8);
    for (u32 coefficient = 0; matches && coefficient < 8; ++coefficient) {
      matches = static_cast<s8>(reader.u8At(address + coefficient)) == kFirPresets[preset][coefficient];
    }
    if (matches) {
      return preset;
    }
  }
  return 0;
}

}  // namespace

const char* versionName(Version version) {
  switch (version) {
    case Version::CosmoGang:
      return "Cosmo Gang";
    case Version::DualOrb:
      return "Dual Orb";
    case Version::Modern:
      return "Later Driver";
  }
  return "Unknown";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto loadSequence = findBytePattern(reader, kLoadSequence);
  const auto executeCommand = findBytePattern(reader, kExecuteCommand);
  const auto setDsp = findBytePattern(reader, kSetDspRegisters);
  const auto loadInstrument = findBytePattern(reader, kLoadInstrument);
  const auto loadTuning = findBytePattern(reader, kLoadTuning);
  if (!loadSequence || !executeCommand || !setDsp || !loadInstrument || !loadTuning) {
    return std::nullopt;
  }

  const u16 sequenceList = reader.le16(*loadSequence + 1);
  const u16 commandTable = reader.le16(*executeCommand + 18);
  if (!reader.has(commandTable, 0x80)) {
    return std::nullopt;
  }
  const u16 command0 = reader.le16(commandTable);
  const Version version = command0 == reader.le16(commandTable + 32)
                              ? Version::CosmoGang
                              : (command0 == reader.le16(commandTable + 10) ? Version::DualOrb : Version::Modern);
  const auto song = selectSong(reader, sequenceList);
  const auto alternatePan = panTableFromHandler(reader, commandTable, 0xd0);
  const auto defaultPan = panTableFromHandler(reader, commandTable, 0xd1);
  if (!song || !alternatePan || !defaultPan) {
    return std::nullopt;
  }

  const u16 dspTable = static_cast<u16>(reader.u8At(*setDsp + 3) | (reader.u8At(*setDsp + 1) << 8));
  const u16 adsr1 = reader.le16(*loadInstrument + 11);
  const u16 adsr2 = reader.le16(*loadInstrument + 17);
  const u16 tuningHigh = reader.le16(*loadTuning + 3);
  const u16 tuningLow = reader.le16(*loadTuning + 11);
  if (!reader.has(dspTable, 16) || !reader.has(adsr1, 0x100) || !reader.has(adsr2, 0x100) ||
      !reader.has(tuningHigh, 0x100) || !reader.has(tuningLow, 0x100)) {
    return std::nullopt;
  }

  return Layout{
      .version = version,
      .sequenceListAddress = sequenceList,
      .sequenceHeaderAddress = song->header,
      .commandTableAddress = commandTable,
      .spcDirAddress = static_cast<u16>(reader.u8At(dspTable + 5) << 8),
      .adsr1TableAddress = adsr1,
      .adsr2TableAddress = adsr2,
      .tuningHighTableAddress = tuningHigh,
      .tuningLowTableAddress = tuningLow,
      .alternatePanTableAddress = *alternatePan,
      .defaultPanTableAddress = *defaultPan,
      .echoDelay = reader.u8At(dspTable + 7),
      .echoFilter = firPreset(reader, dspTable + 8),
      .songIndex = song->index,
      .tracks = song->tracks,
  };
}

}  // namespace vgmtrans::formats::prism_snes

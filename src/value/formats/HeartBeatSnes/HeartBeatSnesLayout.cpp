/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatSnes/HeartBeatSnes.h"

#include "value/scan/BytePattern.h"

#include <array>

namespace vgmtrans::formats::heartbeat_snes {

using namespace core;

namespace {

constexpr auto kSongList = makeMaskedBytePattern(
    "\xee\xf6\x00\x00\xc4\x00\xf6\x00\x00\xc4\x00\xf8\x00\xdd\xd5\x00\x00\x8d\x00", "xx??x?x??x?x?xx??xx");

constexpr auto kSequenceBase =
    makeMaskedBytePattern("\xf5\x00\x00\x28\x0f\xfd\xf6\x00\x00\xc4\x00\xf6\x00\x00\xc4\x00", "x??xxxx??x?x??x?");

constexpr auto kLoadDir = makeMaskedBytePattern("\xe8\x00\x8d\x5d\x3f\x00\x00\xe8\x00", "x?xxx??xx");

constexpr auto kLoadSrcn = makeMaskedBytePattern("\x3f\x00\x00\x8d\x06\xcf\xfd\x6d\xf7\x08\xd5\x00\x00\xeb\x36\xf6"
                                                 "\x00\x00\x28\x0f\x8d\x10\xcf\xee\x60\x97\x08\xfc\x6d\xfd\xf6\x00"
                                                 "\x00\x8d\x04\x3f\x00\x00\xee",
                                                 "x??xxxxx"
                                                 "x?x??x?x"
                                                 "??xxxxxx"
                                                 "xx?xxxx?"
                                                 "?xxx??x");

constexpr std::array<u8, 40> kLengthsDq3{
    0, 0, 1, 7, 1, 2, 3, 1, 0, 1, 2, 1, 0, 1, 1, 3, 0, 1, 2, 3,
    3, 3, 0, 1, 2, 3, 0, 0, 0, 8, 2, 1, 2, 2, 0, 0, 0, 1, 0, 1,
};
constexpr std::array<u8, 40> kLengthsDq6{
    0, 0, 1, 7, 1, 2, 3, 1, 0, 1, 2, 1, 2, 1, 1, 3, 0, 1, 2, 3,
    3, 3, 0, 1, 2, 3, 3, 0, 0, 8, 2, 1, 2, 2, 0, 0, 0, 1, 0, 1,
};

template <size_t Size>
[[nodiscard]] bool contains(ByteReader reader, const std::array<u8, Size>& bytes) {
  if (reader.size() < bytes.size()) {
    return false;
  }
  for (u32 offset = 0; offset <= reader.size() - bytes.size(); ++offset) {
    if (matchesBytes(reader, offset, std::span<const u8>{bytes})) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool validHeader(ByteReader reader, u16 address, u16* instrumentTable = nullptr) {
  if (!reader.has(address, 4)) {
    return false;
  }
  const u16 instrumentRelative = reader.le16(address);
  const u16 instruments = static_cast<u16>(address + instrumentRelative);
  if (instrumentRelative == 0) {
    return false;
  }

  u32 tracks = 0;
  for (u32 index = 0; index < kTrackCount; ++index) {
    const u32 pointer = address + 2 + index * 2;
    if (!reader.has(pointer, 2)) {
      return false;
    }
    const u16 relative = reader.le16(pointer);
    if (relative == 0) {
      break;
    }
    if (!reader.has(static_cast<u16>(address + relative), 1)) {
      return false;
    }
    ++tracks;
  }
  if (tracks == 0) {
    return false;
  }
  if (instrumentTable != nullptr) {
    *instrumentTable = instruments;
  }
  return true;
}

}  // namespace

const char* versionName(Version version) {
  switch (version) {
    case Version::DragonQuest3:
      return "Dragon Quest III";
    case Version::DragonQuest6:
      return "Dragon Quest VI";
  }
  return "Unknown";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }

  const bool dq3 = contains(reader, kLengthsDq3);
  const bool dq6 = contains(reader, kLengthsDq6);
  if (dq3 == dq6) {
    return std::nullopt;
  }

  const auto songLoader = findBytePattern(reader, kSongList);
  const auto sequenceLoader = findBytePattern(reader, kSequenceBase);
  const auto dirLoader = findBytePattern(reader, kLoadDir);
  const auto srcnLoader = findBytePattern(reader, kLoadSrcn);
  if (!songLoader || !sequenceLoader || !dirLoader || !srcnLoader) {
    return std::nullopt;
  }

  const u16 songLow = reader.le16(*songLoader + 2);
  const u16 songHigh = reader.le16(*songLoader + 7);
  if (songLow >= songHigh || songHigh - songLow > 16 || !reader.has(songHigh, songHigh - songLow)) {
    return std::nullopt;
  }
  const u32 songCount = songHigh - songLow;
  const auto songHeader = [&](u32 index) {
    return static_cast<u16>(reader.u8At(songLow + index) | (reader.u8At(songHigh + index) << 8));
  };

  const u8 sequencePointer = reader.u8At(*sequenceLoader + 10);
  if (reader.u8At(*sequenceLoader + 15) != static_cast<u8>(sequencePointer + 1) || !reader.has(sequencePointer, 2)) {
    return std::nullopt;
  }
  const u16 liveSequenceHeader = reader.le16(sequencePointer);
  u16 sequenceHeader = liveSequenceHeader;
  u16 instrumentTable = 0;
  std::optional<u8> songIndex;

  // The pointer bank's leading, zero-terminated run contains songs. Later
  // entries are resident SFX sequences. $3b/$3c is only a work pointer for the
  // group currently serviced by the driver, so an SFX can legitimately be
  // there when the SPC is captured (Town is a common example).
  u32 musicCount = 0;
  while (musicCount < songCount && songHeader(musicCount) != 0) {
    ++musicCount;
  }
  const auto selectSong = [&](u32 index) {
    const u16 candidate = songHeader(index);
    u16 candidateInstruments = 0;
    if (candidate == 0 || !validHeader(reader, candidate, &candidateInstruments)) {
      return false;
    }
    sequenceHeader = candidate;
    instrumentTable = candidateInstruments;
    songIndex = static_cast<u8>(index);
    return true;
  };

  for (u32 index = 0; index < musicCount && !songIndex; ++index) {
    if (songHeader(index) == liveSequenceHeader) {
      static_cast<void>(selectSong(index));
    }
  }

  // When the live work pointer belongs to an SFX group, recover the active
  // music song from the persistent per-group state used by the loader itself.
  const u16 groupSongs = reader.le16(*songLoader + 15);
  if (!songIndex && reader.has(groupSongs, songCount)) {
    for (u32 group = 0; group < songCount && !songIndex; ++group) {
      const u8 state = reader.u8At(groupSongs + group);
      const u32 index = state & 0x0f;
      if (state != 0xff && index < musicCount) {
        static_cast<void>(selectSong(index));
      }
    }
  }
  for (u32 index = 0; index < musicCount && !songIndex; ++index) {
    static_cast<void>(selectSong(index));
  }

  // Some dedicated SFX snapshots have no usable song entry. Preserve support
  // for those only as a last resort, after exhausting the stable song state.
  if (!songIndex && validHeader(reader, liveSequenceHeader, &instrumentTable)) {
    const u8 groupPointer = reader.u8At(*songLoader + 12);
    if (!reader.has(groupPointer, 1)) {
      return std::nullopt;
    }
    const u8 group = reader.u8At(groupPointer);
    if (!reader.has(static_cast<u16>(groupSongs + group), 1)) {
      return std::nullopt;
    }
    sequenceHeader = liveSequenceHeader;
    songIndex = reader.u8At(static_cast<u16>(groupSongs + group)) & 0x0f;
  }
  if (!songIndex) {
    return std::nullopt;
  }

  const u16 spcDir = static_cast<u16>(reader.u8At(*dirLoader + 1) << 8);
  const u16 srcnTable = reader.le16(*srcnLoader + 31);
  if (!reader.has(spcDir, 4) || !reader.has(srcnTable, 1)) {
    return std::nullopt;
  }

  return Layout{
      .version = dq3 ? Version::DragonQuest3 : Version::DragonQuest6,
      .sequenceHeaderAddress = sequenceHeader,
      .instrumentTableAddress = instrumentTable,
      .spcDirAddress = spcDir,
      .srcnTableAddress = srcnTable,
      .songIndex = *songIndex,
  };
}

}  // namespace vgmtrans::formats::heartbeat_snes

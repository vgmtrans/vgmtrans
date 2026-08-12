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

constexpr u32 kGroupCount = 7;

constexpr auto kLoadSong = makeMaskedBytePattern(
    "\xee\xf6\x00\x00\xc4\x00\xf6\x00\x00\xc4\x00\xf8\x00\xdd\xd5\x00\x00\x8d\x00", "xx??x?x??x?x?xx??xx");

constexpr auto kSelectSequence =
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
[[nodiscard]] bool containsBytes(ByteReader reader, const std::array<u8, Size>& bytes) {
  if (reader.size() < bytes.size()) {
    return false;
  }
  for (u32 offset = 0; offset <= reader.size() - bytes.size(); ++offset) {
    if (matchesBytes(reader, offset, bytes)) {
      return true;
    }
  }
  return false;
}

struct SequenceLocation {
  u16 header;
  u16 instrumentTable;
  u8 trackCount;
  u8 songIndex;
};

[[nodiscard]] std::optional<SequenceLocation> readSequence(ByteReader reader, u16 header, u8 songIndex) {
  if (!reader.has(header, 4)) {
    return std::nullopt;
  }
  const u16 instrumentRelative = reader.le16(header);
  if (instrumentRelative == 0) {
    return std::nullopt;
  }

  u8 trackCount = 0;
  while (trackCount < kTrackCount) {
    const u32 pointer = header + 2 + trackCount * 2;
    if (!reader.has(pointer, 2)) {
      return std::nullopt;
    }
    const u16 relative = reader.le16(pointer);
    if (relative == 0) {
      break;
    }
    if (!reader.has(static_cast<u16>(header + relative), 1)) {
      return std::nullopt;
    }
    ++trackCount;
  }
  if (trackCount == 0) {
    return std::nullopt;
  }
  return SequenceLocation{
      .header = header,
      .instrumentTable = static_cast<u16>(header + instrumentRelative),
      .trackCount = trackCount,
      .songIndex = songIndex,
  };
}

[[nodiscard]] std::optional<SequenceLocation> selectSequence(ByteReader reader, u32 loader, u16 liveHeader) {
  const u16 songLow = reader.le16(loader + 2);
  const u16 songHigh = reader.le16(loader + 7);
  if (songLow >= songHigh || songHigh - songLow > 16 || !reader.has(songHigh, songHigh - songLow)) {
    return std::nullopt;
  }

  const auto songHeader = [&](u32 index) {
    return static_cast<u16>(reader.u8At(songLow + index) | (reader.u8At(songHigh + index) << 8));
  };
  const auto readSong = [&](u32 index) { return readSequence(reader, songHeader(index), static_cast<u8>(index)); };

  u32 musicCount = 0;
  while (musicCount < songHigh - songLow) {
    const u16 candidate = songHeader(musicCount);
    if (candidate == 0) {
      break;
    }
    if (candidate == liveHeader) {
      if (const auto selected = readSong(musicCount)) {
        return selected;
      }
    }
    ++musicCount;
  }

  // The driver services seven groups. Its live sequence pointer is only a
  // work register for the group currently being updated, so use persistent
  // group state when that pointer happens to belong to a resident SFX.
  const u16 groupSongs = reader.le16(loader + 15);
  if (reader.has(groupSongs, kGroupCount)) {
    for (u32 group = 0; group < kGroupCount; ++group) {
      const u8 state = reader.u8At(groupSongs + group);
      const u32 index = state & 0x0f;
      if (state == 0xff || index >= musicCount) {
        continue;
      }
      if (const auto selected = readSong(index)) {
        return selected;
      }
    }
  }

  // If live state is unavailable, keep the format usable with the first valid song.
  for (u32 index = 0; index < musicCount; ++index) {
    if (const auto selected = readSong(index)) {
      return selected;
    }
  }

  // Dedicated SFX snapshots occasionally have no usable music entry.
  const u8 activeGroupPointer = reader.u8At(loader + 12);
  if (!reader.has(activeGroupPointer, 1)) {
    return std::nullopt;
  }
  const u8 group = reader.u8At(activeGroupPointer);
  if (group >= kGroupCount || !reader.has(groupSongs + group, 1)) {
    return std::nullopt;
  }
  return readSequence(reader, liveHeader, reader.u8At(groupSongs + group) & 0x0f);
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

  const bool dq3 = containsBytes(reader, kLengthsDq3);
  const bool dq6 = containsBytes(reader, kLengthsDq6);
  if (dq3 == dq6) {
    return std::nullopt;
  }
  const Version version = dq3 ? Version::DragonQuest3 : Version::DragonQuest6;

  const auto songLoader = findBytePattern(reader, kLoadSong);
  const auto sequenceLoader = findBytePattern(reader, kSelectSequence);
  const auto dirLoader = findBytePattern(reader, kLoadDir);
  const auto srcnLoader = findBytePattern(reader, kLoadSrcn);
  if (!songLoader || !sequenceLoader || !dirLoader || !srcnLoader) {
    return std::nullopt;
  }

  const u8 sequencePointer = reader.u8At(*sequenceLoader + 10);
  if (reader.u8At(*sequenceLoader + 15) != static_cast<u8>(sequencePointer + 1) || !reader.has(sequencePointer, 2)) {
    return std::nullopt;
  }
  const auto sequence = selectSequence(reader, *songLoader, reader.le16(sequencePointer));
  if (!sequence) {
    return std::nullopt;
  }

  const u16 spcDir = static_cast<u16>(reader.u8At(*dirLoader + 1) << 8);
  const u16 srcnTable = reader.le16(*srcnLoader + 31);
  if (!reader.has(spcDir, 4) || !reader.has(srcnTable, 1)) {
    return std::nullopt;
  }

  return Layout{
      .version = version,
      .sequenceHeaderAddress = sequence->header,
      .instrumentTableAddress = sequence->instrumentTable,
      .spcDirAddress = spcDir,
      .srcnTableAddress = srcnTable,
      .songIndex = sequence->songIndex,
      .trackCount = sequence->trackCount,
  };
}

}  // namespace vgmtrans::formats::heartbeat_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ChunSnes/ChunSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <limits>
#include <string_view>

namespace vgmtrans::formats::chun_snes {

using namespace core;
using namespace std::string_view_literals;

namespace {

constexpr MaskedBytePattern kLoadSongSummer{
    "\xd5\x1d\x05\xc9\x66\x05\x2d\xe5\x8f\x21\xc4\xc0\xe5\x90\x21\xc4"
    "\xc1\xae\x1c\x90\x02\xab\xc1\x60\x84\xc0\xc4\xc0\x90\x02\xab\xc1"
    "\x8d\x00\xf7\xc0\x2d\xfc\xf7\xc0\xc4\xc1\xae\xc4\xc0\x04\xc1\xf0"
    "\x9a\xe8\xff\xd5\x15\x05\xe8\x00\xd5\x45\x05\xd5\x4d\x05\xd5\x35"
    "\x05\xd5\x55\x05\x8d\x00\xf7\xc0\xfc\xd5\x25\x05"sv,
    "x??x??xx??x?x??x?xxxxx?xx?x?xxx?xxx?xxx?x?xx?x?x?xxx??xxx??x??x??x??xxx?xx??"sv,
};

constexpr MaskedBytePattern kLoadSongWinter{
    "\xc9\xf8\x03\xfd\xf6\xc7\x04\x8f\xd5\xa0\x8f\x05\xa1\x8d\x06\xcf\x7a\xa0\xda\xa0\x8d\x01\xf7\xa0\x2d\xfc\xf7\xa0\xc4\xa1\xae\xc4\xa0\xe8\x00\xd5\xdf\x03\xd5\xc7\x03\xd5\xe7\x03\xe8\x02\xd5\xd7\x03\x8d\x00\xf7\xa0\xfc\xd5\xb7\x03"sv,
    "x??xx??x??x??xxxx?x?xxx?xxx?x?xx?xxx??x??x??xxx??xxx?xx??"sv,
};

constexpr MaskedBytePattern kLoadSongWinterV3{
    "\xc9\x09\x04\xfd\xf6\xd8\x04\x8f\xe6\xa0\x8f\x06\xa1\x8d\x06\xcf\x7a\xa0\xda\xa0\x8d\x05\xf7\xa0\x08\x08\xd7\xa0\x8d\x01\xf7\xa0\x2d\xfc\xf7\xa0\xc4\xa1\xae\xc4\xa0\xe8\x00\xd5\xef\x03\xd5\xd7\x03\xd5\xf7\x03\xe8\x02\xd5\xe7\x03\x8d\x00\xf7\xa0\xfc\xd5\xbf\x03\xd5\xc7\x03"sv,
    "x??xx??x??x??xxxx?x?xxx?xxx?xxx?xxx?x?xx?xxx??x??x??xxx??xxx?xx??x??"sv,
};

constexpr MaskedBytePattern kSaveSongIndexSummer{
    "\x3f\xc3\x0f\xb0\xce\xd5\x1d\x05\xc9\x66\x05\x2d"sv,
    "x??x?x??x??x"sv,
};

constexpr MaskedBytePattern kDspInit{
    "\x6c\xf0\x7d\x01\x6d\xf7\x1c\x7f\x0c\x7f\x3c\x00\x2c\x00\x4c\x00\x5c\x00\x6c\x23\x0d\x00\x2d\x00\x3d\x00\x4d\x00\x5d\x07\x0d\x00\x00"sv,
    "x?x?x?x?x?x?x?x?x?x?x?x?x?x?x?x?x"sv,
};

constexpr MaskedBytePattern kProgramSummer{
    "\x3f\xaa\x15\x2d\xf5\xdd\x03\xfd\xf6\x1d\x05\xfd\x4d\x8f\x03\xc4\x8f\x02\xc5\xcd\x00\xad\x00\xf0\x0e\xe7\xc4\xbc\x6d\x8d\x00\x7a\xc4\xda\xc4\xee\xdc\x2f\xee\xce\xae\xbc\xfd\xf7\xc4\xfd\xf6\x67\x05\x68\xff\xd0\x02\x00\xbc\xd5\x0e\x04\x8d\x04\x3f\x65\x17\x8d\x08\xcf\x8f\x2f\xc4\x8f\x06\xc5\x7a\xc4\xda\xc4"sv,
    "x??xx??xx??xxx??x??xxxxxx?xxxxx?x?xxxxxxxxxx?xx??xxxxxxx??xxx??xxxx??x??x?x?"sv,
};

constexpr MaskedBytePattern kProgramWinter{
    "\x3f\xbd\x19\x2d\xf5\xa3\x02\xfd\xf6\xaf\x03\xfd\x4d\xe5\xe9\x28\xc4\xa4\xe5\xea\x28\xc4\xa5\xcd\x00\xad\x00\xf0\x0f\xe7\xa4\xbc\xbc\x6d\x8d\x00\x7a\xa4\xda\xa4\xee\xdc\x2f\xed\xce\xae\xbc\xbc\xfd\xf7\xa4\x65\x84\x01\xf0\x08\xfd\xf6\xff\x03\x68\xff\xd0\x15\xf5\xcb\x02\x08\x80\xd5\xcb\x02\x3f\x9e\x1a\x3f\x3e\x1c\xb0\x04\xfd\x3f\x93\x1a\x6f\xd5\xcc\x02\x8d\x04\x3f\x23\x1b\x8d\x08\xcf\x8f\x35\xa4\x8f\x05\xa5\x7a\xa4\xda\xa4"sv,
    "x??xx??xx??xxx??x?x??x?xxxxxxx?xxxxxx?x?xxxxxxxxxx?x??xxxx??xxxxx??xxx??x??x??xxxx??xx??xxx??xxxx??x??x?x?"sv,
};

// Both table finders match the code that consumes the table, rather than
// guessing from data. This remains stable when a game relocates the driver.
constexpr MaskedBytePattern kPitchEnvelopeReader{
    "\xf4\x78\x68\xff\xf0\x31\x9b\x8c\xd0\x2d\xfb\x78\xf6\x5d\x0a\xc4\xa0\xf6\x5e\x0a\xc4\xa1\xfb\x79\xf7\xa0\x68\x80\xf0\x19"sv,
    "x?xxx?x?x?x?x??x?x??x?x?x?xxx?"sv,
};

constexpr MaskedBytePattern kMiniSequenceReader{
    "\x20\x1c\x4d\x5d\xf5\x31\x09\xc4\xa6\xf5\x32\x09\xc4\xa7\xce"sv,
    "xxxxx??x?x??x?x"sv,
};

constexpr MaskedBytePattern kDurationScriptReader{
    "\x3f\xd8\x18\x68\xff\xf0\x15\x1c\xfd\xf6\xcf\x08\xd4\x78\xf6\xd0\x08\xd4\x79"sv,
    "x??xxx?xxx??x?x??x?"sv,
};

constexpr MaskedBytePattern kGainEnvelopeReader{
    "\xf4\x61\x9c\xd4\x61\xd0\x2b\xfb\x49\xf6\xe7\x08\xc4\xc0\xf6\xe8\x08\xc4\xc1\xfb\x60\xf7\xc0\x68\xff"sv,
    "x?xx?x?x?x??x?x??x?x?x?xx"sv,
};

[[nodiscard]] bool validHeader(ByteReader reader, Version version, u32 address) {
  if (!reader.has(address, 4)) {
    return false;
  }
  const u8 tracks = reader.u8At(address + 1);
  if (tracks == 0 || tracks > kTrackCount || !reader.has(address + 2, tracks * 2)) {
    return false;
  }
  for (u32 track = 0; track < tracks; ++track) {
    const u16 raw = reader.le16(address + 2 + track * 2);
    const u32 start = version == Version::Summer ? raw : address + raw;
    if (!reader.has(start, 1)) {
      return false;
    }
  }
  return true;
}

struct SongChoice {
  u8 index = 0;
  u16 header = 0;
};

[[nodiscard]] std::optional<SongChoice> selectSong(ByteReader reader, Version version, u16 songList) {
  const u32 entrySize = version == Version::Summer ? 2 : 6;
  const u32 headerOffset = version == Version::Summer ? 0 : 1;
  const u16 current = reader.le16(0);
  std::optional<SongChoice> nearest;
  u32 nearestDistance = std::numeric_limits<u32>::max();
  std::optional<SongChoice> first;

  for (u32 index = 0; index < 0x7f; ++index) {
    const u32 entry = songList + index * entrySize;
    if (!reader.has(entry + headerOffset, 2)) {
      break;
    }
    const u16 header = reader.le16(entry + headerOffset);
    if (!validHeader(reader, version, header)) {
      continue;
    }
    const SongChoice candidate{.index = static_cast<u8>(index), .header = header};
    first = first ? first : std::optional{candidate};
    const u16 rawStart = reader.le16(header + 2);
    const u32 start = version == Version::Summer ? rawStart : header + rawStart;
    if (current >= start && current - start < nearestDistance) {
      nearest = candidate;
      nearestDistance = current - start;
    }
  }
  return nearest ? nearest : first;
}

[[nodiscard]] u16 resolveInstrumentSet(ByteReader reader, u16 table, u8 index, Version version) {
  u32 address = table;
  for (u32 set = 0; set < index; ++set) {
    if (!reader.has(address, version == Version::Summer ? 1 : 2)) {
      return 0;
    }
    address += reader.u8At(address) + (version == Version::Summer ? 1 : 2);
  }
  return reader.has(address, version == Version::Summer ? 1 : 2) ? static_cast<u16>(address) : 0;
}

[[nodiscard]] bool instrumentSetHasPrograms(ByteReader reader, u16 set, u16 srcns, Version version) {
  if (set == 0 || !reader.has(set, version == Version::Summer ? 1 : 2)) {
    return false;
  }
  const u32 count = reader.u8At(set);
  const u32 mapping = set + (version == Version::Summer ? 1 : 2);
  if (count == 0 || !reader.has(mapping, count)) {
    return false;
  }
  for (u32 program = 0; program < count; ++program) {
    const u8 global = reader.u8At(mapping + program);
    if (reader.has(srcns + global, 1) && reader.u8At(srcns + global) != 0xff) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] u16 winterPitchReference(ByteReader reader, u16 miniTable) {
  if (miniTable < 28 || !reader.has(miniTable - 28, 28)) {
    return 0x1ede;
  }
  const u16 first = reader.le16(miniTable - 28);
  u16 previous = first;
  for (u32 i = 1; i < 14; ++i) {
    const u16 value = reader.le16(miniTable - 28 + i * 2);
    if (value <= previous) {
      return 0x1ede;
    }
    previous = value;
  }
  return first >= 0x1800 && first <= 0x2800 ? first : 0x1ede;
}

}  // namespace

const char* versionName(Version version) {
  switch (version) {
    case Version::Summer:
      return "Earlier / Summer";
    case Version::Winter:
      return "Earlier / Winter";
    case Version::WinterV3:
      return "Earlier / Winter SN2";
  }
  return "Unknown";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }

  Version version;
  u16 songList = 0;
  if (const auto loadV3 = findBytePattern(reader, kLoadSongWinterV3)) {
    version = Version::WinterV3;
    songList = static_cast<u16>(reader.u8At(*loadV3 + 8) | (reader.u8At(*loadV3 + 11) << 8));
  } else if (const auto loadWinter = findBytePattern(reader, kLoadSongWinter)) {
    version = Version::Winter;
    songList = static_cast<u16>(reader.u8At(*loadWinter + 8) | (reader.u8At(*loadWinter + 11) << 8));
  } else if (const auto loadSummer = findBytePattern(reader, kLoadSongSummer)) {
    version = Version::Summer;
    const u16 pointer = reader.le16(*loadSummer + 8);
    if (!reader.has(pointer, 2)) {
      return std::nullopt;
    }
    songList = reader.le16(pointer);
  } else {
    return std::nullopt;
  }

  auto song = selectSong(reader, version, songList);
  if (!song) {
    return std::nullopt;
  }
  if (version == Version::Summer) {
    if (const auto save = findBytePattern(reader, kSaveSongIndexSummer)) {
      const u16 indexes = reader.le16(*save + 6);
      const u16 slotAddress = reader.le16(*save + 9);
      if (reader.has(slotAddress, 1) && reader.has(indexes + reader.u8At(slotAddress), 1)) {
        const u8 liveIndex = reader.u8At(indexes + reader.u8At(slotAddress));
        const u32 entry = songList + liveIndex * 2;
        if (reader.has(entry, 2) && validHeader(reader, version, reader.le16(entry))) {
          song = SongChoice{.index = liveIndex, .header = reader.le16(entry)};
        }
      }
    }
  }

  const auto dsp = findBytePattern(reader, kDspInit);
  const auto program = findBytePattern(reader, version == Version::Summer ? kProgramSummer : kProgramWinter);
  if (!dsp || !program) {
    return std::nullopt;
  }

  u16 instrumentSets = 0;
  u16 srcns = 0;
  u16 sampleInfo = 0;
  u8 instrumentSetIndex = 0;
  if (version == Version::Summer) {
    instrumentSets = static_cast<u16>(reader.u8At(*program + 14) | (reader.u8At(*program + 17) << 8));
    srcns = reader.le16(*program + 47);
    sampleInfo = static_cast<u16>(reader.u8At(*program + 67) | (reader.u8At(*program + 70) << 8));
  } else {
    const u16 tablePointer = reader.le16(*program + 14);
    if (!reader.has(tablePointer, 2)) {
      return std::nullopt;
    }
    instrumentSets = reader.le16(tablePointer);
    srcns = reader.le16(*program + 58);
    sampleInfo = static_cast<u16>(reader.u8At(*program + 97) | (reader.u8At(*program + 100) << 8));
  }
  const u16 trackSlotAddress = reader.le16(*program + 5);
  const u16 setIndexAddress = reader.le16(*program + 9);
  if (reader.has(trackSlotAddress, 1)) {
    u8 slot = reader.u8At(trackSlotAddress);
    slot = slot == 0xff ? 0 : slot;
    if (reader.has(setIndexAddress + slot, 1)) {
      instrumentSetIndex = reader.u8At(setIndexAddress + slot);
    }
  }
  u16 instrumentSet = resolveInstrumentSet(reader, instrumentSets, instrumentSetIndex, version);
  if (!instrumentSetHasPrograms(reader, instrumentSet, srcns, version)) {
    const u16 songSet = resolveInstrumentSet(reader, instrumentSets, song->index, version);
    if (instrumentSetHasPrograms(reader, songSet, srcns, version)) {
      instrumentSet = songSet;
    }
  }
  if (instrumentSet == 0 || !reader.has(srcns, 1) || !reader.has(sampleInfo, 8)) {
    return std::nullopt;
  }

  u16 pitchEnvelopes = 0;
  if (const auto code = findBytePattern(reader, kPitchEnvelopeReader)) {
    pitchEnvelopes = reader.le16(*code + 13);
  }
  u16 miniSequences = 0;
  if (const auto code = findBytePattern(reader, kMiniSequenceReader)) {
    miniSequences = reader.le16(*code + 5);
  }
  u16 durationScripts = 0;
  if (const auto code = findBytePattern(reader, kDurationScriptReader)) {
    durationScripts = reader.le16(*code + 10);
  }
  u16 gainEnvelopes = 0;
  if (const auto code = findBytePattern(reader, kGainEnvelopeReader)) {
    gainEnvelopes = reader.le16(*code + 10);
  }

  const u8 dir = reader.u8At(*dsp + 29);
  return Layout{
      .version = version,
      .songListAddress = songList,
      .sequenceHeaderAddress = song->header,
      .instrumentSetAddress = instrumentSet,
      .srcnTableAddress = srcns,
      .sampleInfoTableAddress = sampleInfo,
      .spcDirAddress = static_cast<u16>(dir << 8),
      .pitchEnvelopeTableAddress = pitchEnvelopes,
      .miniSequenceTableAddress = miniSequences,
      .durationScriptTableAddress = durationScripts,
      .gainEnvelopeTableAddress = gainEnvelopes,
      .pitchReference =
          static_cast<u16>(version == Version::Summer ? 0x1ede : winterPitchReference(reader, miniSequences)),
      .songIndex = song->index,
      .echo = EchoState{.left = static_cast<s8>(reader.u8At(*dsp + 13)),
                        .right = static_cast<s8>(reader.u8At(*dsp + 11)),
                        .feedback = static_cast<s8>(reader.u8At(*dsp + 21)),
                        .delay = static_cast<u8>(reader.u8At(*dsp + 3) & 0x0f)},
  };
}

}  // namespace vgmtrans::formats::chun_snes

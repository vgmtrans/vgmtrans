/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MP2k/MP2k.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <set>
#include <vector>

namespace vgmtrans::formats::mp2k {

using namespace core;

namespace {

constexpr std::array<u32, 16> kSampleRates{
    0, 5734, 7884, 10512, 13379, 15768, 18157, 21024, 26758, 31536, 36314, 40137, 42048, 0, 0, 0,
};
constexpr u32 kDefaultSampleRateIndex = 4;
constexpr u8 kDefaultDirectSoundMasterVolume = 15;
constexpr u32 kPlayerEntrySize = 12;
constexpr u32 kSongEntrySize = 8;
constexpr u8 kMaxTracks = 24;
constexpr u32 kMaxSongs = 4096;
constexpr u32 kCompatibleProbeSongs = 512;
constexpr u32 kMinimumCompatibleSongs = 4;

constexpr std::array<u8, 30> kSongSelectV1{
    0x00, 0xb5, 0x00, 0x04, 0x07, 0x4a, 0x08, 0x49, 0x40, 0x0b, 0x40, 0x18, 0x83, 0x88, 0x59,
    0x00, 0xc9, 0x18, 0x89, 0x00, 0x89, 0x18, 0x0a, 0x68, 0x01, 0x68, 0x10, 0x1c, 0x00, 0xf0,
};
constexpr std::array<u8, 30> kSongSelectV2{
    0x00, 0xb5, 0x00, 0x04, 0x07, 0x4b, 0x08, 0x49, 0x40, 0x0b, 0x40, 0x18, 0x82, 0x88, 0x51,
    0x00, 0x89, 0x18, 0x89, 0x00, 0xc9, 0x18, 0x0a, 0x68, 0x01, 0x68, 0x10, 0x1c, 0x00, 0xf0,
};
constexpr std::string_view kExactPatternMask = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

[[nodiscard]] std::optional<u32> romOffset(u32 address, ByteReader reader) {
  if ((address & 0xfe000000) != 0x08000000) {
    return std::nullopt;
  }
  const u32 offset = address & 0x01ffffff;
  return offset < reader.size() ? std::optional<u32>{offset} : std::nullopt;
}

struct PlayerTable {
  u32 offset = 0;
  u32 count = 0;

  [[nodiscard]] std::optional<u8> trackLimit(ByteReader reader, u32 songEntry) const {
    const u32 player = reader.le16(songEntry + 4);
    if (player >= count || !reader.has(offset + static_cast<u64>(player) * kPlayerEntrySize, kPlayerEntrySize)) {
      return std::nullopt;
    }
    const u8 tracks = reader.u8At(offset + player * kPlayerEntrySize + 8);
    return tracks > 0 && tracks <= kMaxTracks ? std::optional<u8>{tracks} : std::nullopt;
  }
};

struct EngineSettings {
  Mp2kEngine engine;
  std::optional<PlayerTable> players;
};

[[nodiscard]] std::optional<EngineSettings> readEngineSettings(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 12)) {
    return std::nullopt;
  }
  const u32 value = reader.le32(offset);
  const u32 polyphony = (value >> 8) & 0x0f;
  const u32 encodedRate = (value >> 16) & 0x0f;
  const u8 encodedMasterVolume = static_cast<u8>((value >> 12) & 0x0f);
  const u8 encodedDacMode = static_cast<u8>((value >> 20) & 0x0f);
  const s32 dacBits = encodedDacMode == 0 ? 8 : 17 - encodedDacMode;
  const u32 playerCount = reader.le32(offset + 4);
  const auto tableBase = romOffset(reader.le32(offset + 8), reader);
  if (polyphony >= 13 || encodedRate > 12 || dacBits < 6 || dacBits > 9 || playerCount >= 256 || !tableBase) {
    return std::nullopt;
  }
  const u64 songTable = static_cast<u64>(*tableBase) + static_cast<u64>(playerCount) * kPlayerEntrySize;
  if (songTable > reader.size() || !reader.has(songTable, 4)) {
    return std::nullopt;
  }

  const u32 rateIndex = encodedRate == 0 ? kDefaultSampleRateIndex : encodedRate;
  const u8 masterVolume = encodedMasterVolume == 0 ? kDefaultDirectSoundMasterVolume : encodedMasterVolume;
  EngineSettings result{
      .engine = {.settingsOffset = offset,
                 .songTableOffset = static_cast<u32>(songTable),
                 .sampleRate = kSampleRates[rateIndex],
                 .directSoundMasterVolume = masterVolume,
                 .dacBits = static_cast<u8>(dacBits),
                 .reverb = static_cast<u8>(value & 0x7f)},
  };
  if (playerCount != 0) {
    result.players = PlayerTable{.offset = *tableBase, .count = playerCount};
  }
  return result;
}

[[nodiscard]] std::optional<u32> settingsForSignature(ByteReader reader, u32 signature) {
  u32 prologue = signature;
  const u32 searchBegin = signature > 0x20 ? signature - 0x20 : 0;
  for (u32 offset = signature; offset >= searchBegin; --offset) {
    if (reader.has(offset, 2) && reader.le16(offset) == 0xb500) {
      prologue = offset;
    }
    if (offset == 0) {
      break;
    }
  }
  if (prologue >= 16 && readEngineSettings(reader, prologue - 16)) {
    return prologue - 16;
  }
  if (prologue >= 32 && readEngineSettings(reader, prologue - 32)) {
    return prologue - 32;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Mp2kSong> readSong(ByteReader reader, u32 index, u32 offset, u8 trackLimit = kMaxTracks) {
  if (!reader.has(offset, 8)) {
    return std::nullopt;
  }
  const u8 headerTracks = reader.u8At(offset);
  const auto bank = romOffset(reader.le32(offset + 4), reader);
  if (headerTracks == 0 || headerTracks > kMaxTracks || trackLimit == 0 || !bank ||
      !reader.has(offset + 8, static_cast<u64>(headerTracks) * 4)) {
    return std::nullopt;
  }
  // MPlayStart stops at the selected player's capacity. Some valid headers
  // leave null pointers beyond that limit, and the driver never reads them.
  const u8 tracks = std::min(headerTracks, trackLimit);
  for (u32 track = 0; track < tracks; ++track) {
    if (!romOffset(reader.le32(offset + 8 + track * 4), reader)) {
      return std::nullopt;
    }
  }
  return Mp2kSong{
      .index = index,
      .offset = offset,
      .bankOffset = *bank,
      .declaredTracks = headerTracks,
      .activeTracks = tracks,
      .reverb = reader.u8At(offset + 3),
  };
}

[[nodiscard]] std::vector<Mp2kBank> readBanks(ByteReader reader, const std::set<u32>& offsets) {
  std::vector<Mp2kBank> banks;
  for (auto it = offsets.begin(); it != offsets.end(); ++it) {
    u32 count = 128;
    if (const auto next = std::next(it); next != offsets.end()) {
      count = std::min<u32>(count, (*next - *it) / 12);
    }
    count = std::min<u32>(count, static_cast<u32>((reader.size() - *it) / 12));
    banks.push_back(Mp2kBank{.offset = *it, .instrumentCount = count});
  }
  return banks;
}

[[nodiscard]] std::optional<Mp2kLayout> readSongTable(ByteReader reader, Mp2kEngine engine,
                                                      std::optional<PlayerTable> players = std::nullopt,
                                                      u8 pointerWord = 0, bool stopOnInvalidSong = false) {
  const u32 table = engine.songTableOffset;
  if (!reader.has(table, 8)) {
    return std::nullopt;
  }

  Mp2kLayout layout{.engine = engine};
  std::set<u32> bankOffsets;
  const u32 availableEntries = static_cast<u32>(std::min<u64>((reader.size() - table) / kSongEntrySize, kMaxSongs));
  bool foundSong = false;
  for (u32 index = 0; index < availableEntries; ++index) {
    const u32 entry = table + index * kSongEntrySize;
    const u32 raw = reader.le32(entry + pointerWord * 4);
    if (raw == 0) {
      continue;
    }
    const auto offset = romOffset(raw, reader);
    if (!offset) {
      if (foundSong) {
        break;
      }
      continue;
    }
    u8 trackLimit = kMaxTracks;
    if (players) {
      const auto limit = players->trackLimit(reader, entry);
      if (!limit) {
        continue;
      }
      trackLimit = *limit;
    }
    auto song = readSong(reader, index, *offset, trackLimit);
    if (!song) {
      if (stopOnInvalidSong) {
        break;
      }
      continue;
    }
    song->reverb = (song->reverb & 0x80) != 0 ? song->reverb & 0x7f : layout.engine.reverb;
    foundSong = true;
    bankOffsets.insert(song->bankOffset);
    layout.songs.push_back(*song);
  }
  if (layout.songs.empty()) {
    return std::nullopt;
  }
  layout.banks = readBanks(reader, bankOffsets);
  return layout;
}

[[nodiscard]] std::optional<Mp2kLayout> readLayout(ByteReader reader, u32 settings) {
  const auto parsed = readEngineSettings(reader, settings);
  if (!parsed) {
    return std::nullopt;
  }
  return readSongTable(reader, parsed->engine, parsed->players);
}

[[nodiscard]] std::optional<Mp2kLayout> readSignatureLayout(ByteReader reader, u32 signature) {
  // SongNumStart loads the player and song-table addresses from these two
  // literals in both SDK variants. Some games relocate or omit the usual
  // engine-settings words while retaining this unmodified routine.
  if (!reader.has(signature + 36, 8)) {
    return std::nullopt;
  }
  const auto playerTable = romOffset(reader.le32(signature + 36), reader);
  const auto songTable = romOffset(reader.le32(signature + 40), reader);
  if (!playerTable || !songTable || *songTable <= *playerTable || (*songTable - *playerTable) % kPlayerEntrySize != 0) {
    return std::nullopt;
  }
  const u32 playerCount = (*songTable - *playerTable) / kPlayerEntrySize;
  if (playerCount == 0 || playerCount >= 256) {
    return std::nullopt;
  }

  return readSongTable(reader,
                       Mp2kEngine{.songTableOffset = *songTable,
                                  .sampleRate = kSampleRates[kDefaultSampleRateIndex],
                                  .directSoundMasterVolume = kDefaultDirectSoundMasterVolume},
                       PlayerTable{.offset = *playerTable, .count = playerCount});
}

struct CompatibleTableCandidate {
  u32 offset = 0;
  u8 pointerWord = 0;
  u32 validSongs = 0;
};

[[nodiscard]] u32 compatibleTableScore(ByteReader reader, u32 table, u8 pointerWord) {
  u32 valid = 0;
  for (u32 index = 0;
       index < kCompatibleProbeSongs && reader.has(table + static_cast<u64>(index) * kSongEntrySize, kSongEntrySize);
       ++index) {
    const u32 raw = reader.le32(table + index * kSongEntrySize + pointerWord * 4);
    if (raw == 0) {
      continue;
    }
    const auto offset = romOffset(raw, reader);
    if (!offset || !readSong(reader, index, *offset)) {
      break;
    }
    ++valid;
  }
  return valid;
}

[[nodiscard]] std::optional<Mp2kLayout> findCompatibleLayout(ByteReader reader) {
  // Some replacement drivers (notably Nintendo R&D1's) consume ordinary
  // MP2k song/voice/WaveData structures but have no SDK song-select signature.
  // A real table is referenced by code and contains several independently
  // valid song headers at an eight-byte stride, which is a much stronger
  // discriminator than scanning data for song-shaped bytes alone.
  std::set<u32> referencedOffsets;
  for (u32 offset = 0; reader.has(offset, 4); offset += 4) {
    if (const auto target = romOffset(reader.le32(offset), reader)) {
      referencedOffsets.insert(*target);
    }
  }

  CompatibleTableCandidate best;
  for (const u32 candidate : referencedOffsets) {
    for (u8 pointerWord = 0; pointerWord < 2; ++pointerWord) {
      const u32 score = compatibleTableScore(reader, candidate, pointerWord);
      if (score > best.validSongs) {
        best = CompatibleTableCandidate{.offset = candidate, .pointerWord = pointerWord, .validSongs = score};
      }
    }
  }
  if (best.validSongs < kMinimumCompatibleSongs) {
    return std::nullopt;
  }

  return readSongTable(reader,
                       Mp2kEngine{.songTableOffset = best.offset,
                                  .sampleRate = kSampleRates[kDefaultSampleRateIndex],
                                  .directSoundMasterVolume = kDefaultDirectSoundMasterVolume},
                       std::nullopt, best.pointerWord, true);
}

}  // namespace

std::vector<Mp2kLayout> findMp2kLayouts(ScanResultBuilder& builder) {
  const ByteReader reader = builder.reader();
  std::vector<Mp2kLayout> layouts;
  std::set<u32> settingsSeen;
  std::set<u32> tablesSeen;

  const auto scanPattern = [&](std::span<const u8> pattern) {
    u32 begin = 0;
    while (const auto signature = findBytePattern(reader, MaskedBytePattern{pattern, kExactPatternMask}, begin)) {
      begin = *signature + 1;
      const auto settings = settingsForSignature(reader, *signature);
      std::optional<Mp2kLayout> layout;
      if (settings && settingsSeen.insert(*settings).second) {
        layout = readLayout(reader, *settings);
      }
      if (!layout) {
        layout = readSignatureLayout(reader, *signature);
      }
      if (layout && tablesSeen.insert(layout->engine.songTableOffset).second) {
        layouts.push_back(std::move(*layout));
      }
    }
  };
  scanPattern(kSongSelectV1);
  scanPattern(kSongSelectV2);
  if (layouts.empty()) {
    if (auto compatible = findCompatibleLayout(reader)) {
      builder.warning("Found MP2k-compatible data used by a replacement sound driver",
                      reader.range(compatible->engine.songTableOffset, 8));
      layouts.push_back(std::move(*compatible));
    }
  }
  return layouts;
}

}  // namespace vgmtrans::formats::mp2k

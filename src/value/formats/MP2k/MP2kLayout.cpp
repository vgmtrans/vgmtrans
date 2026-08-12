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

[[nodiscard]] bool validEngineSettings(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 12)) {
    return false;
  }
  const u32 value = reader.le32(offset);
  const u32 polyphony = (value >> 8) & 0x0f;
  const u32 rate = (value >> 16) & 0x0f;
  const u32 dacMode = (value >> 20) & 0x0f;
  const s32 dacBits = dacMode == 0 ? 8 : 17 - static_cast<s32>(dacMode);
  const u32 songLevels = reader.le32(offset + 4);
  const auto tableBase = romOffset(reader.le32(offset + 8), reader);
  if (polyphony >= 13 || rate > 12 || dacBits < 6 || dacBits > 9 || songLevels >= 256 || !tableBase) {
    return false;
  }
  const u64 table = static_cast<u64>(*tableBase) + static_cast<u64>(songLevels) * 12;
  return table <= reader.size() && reader.has(table, 4);
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
  if (prologue >= 16 && validEngineSettings(reader, prologue - 16)) {
    return prologue - 16;
  }
  if (prologue >= 32 && validEngineSettings(reader, prologue - 32)) {
    return prologue - 32;
  }
  return std::nullopt;
}

struct PlayerTable {
  u32 offset = 0;
  u32 count = 0;
};

[[nodiscard]] std::optional<u8> playerTrackLimit(ByteReader reader, const PlayerTable& players, u32 songEntry) {
  const u32 player = reader.le16(songEntry + 4);
  if (player >= players.count || !reader.has(players.offset + static_cast<u64>(player) * 12, 12)) {
    return std::nullopt;
  }
  const u8 tracks = reader.u8At(players.offset + player * 12 + 8);
  return tracks > 0 && tracks <= 24 ? std::optional<u8>{tracks} : std::nullopt;
}

[[nodiscard]] bool validSong(ByteReader reader, u32 offset, Mp2kSong& song, u8 trackLimit = 24) {
  if (!reader.has(offset, 8)) {
    return false;
  }
  const u8 headerTracks = reader.u8At(offset);
  const auto bank = romOffset(reader.le32(offset + 4), reader);
  if (headerTracks == 0 || headerTracks > 24 || trackLimit == 0 || !bank ||
      !reader.has(offset + 8, static_cast<u64>(headerTracks) * 4)) {
    return false;
  }
  // MPlayStart stops at the selected player's capacity. Some valid headers
  // leave null pointers beyond that limit, and the driver never reads them.
  const u8 tracks = std::min(headerTracks, trackLimit);
  for (u32 track = 0; track < tracks; ++track) {
    if (!romOffset(reader.le32(offset + 8 + track * 4), reader)) {
      return false;
    }
  }
  song.offset = offset;
  song.bankOffset = *bank;
  song.trackCount = tracks;
  song.reverb = reader.u8At(offset + 3);
  return true;
}

void addBanks(ByteReader reader, Mp2kLayout& layout, const std::set<u32>& offsets) {
  for (auto it = offsets.begin(); it != offsets.end(); ++it) {
    u32 count = 128;
    if (const auto next = std::next(it); next != offsets.end()) {
      count = std::min<u32>(count, (*next - *it) / 12);
    }
    count = std::min<u32>(count, static_cast<u32>((reader.size() - *it) / 12));
    layout.banks.push_back(Mp2kBank{.offset = *it, .instrumentCount = count});
  }
}

[[nodiscard]] std::optional<Mp2kLayout> readSongTable(ByteReader reader, Mp2kEngine engine,
                                                      std::optional<PlayerTable> players = std::nullopt) {
  const u32 table = engine.songTableOffset;
  if (!reader.has(table, 8)) {
    return std::nullopt;
  }

  Mp2kLayout layout{.engine = engine};
  std::set<u32> bankOffsets;
  const u32 availableEntries = static_cast<u32>(std::min<u64>((reader.size() - table) / 8, 4096));
  bool foundSong = false;
  for (u32 index = 0; index < availableEntries; ++index) {
    const u32 raw = reader.le32(table + index * 8);
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
    std::optional<u8> trackLimit;
    if (players) {
      trackLimit = playerTrackLimit(reader, *players, table + index * 8);
      if (!trackLimit) {
        continue;
      }
    }
    Mp2kSong song{.index = index};
    if (!validSong(reader, *offset, song, trackLimit.value_or(24))) {
      continue;
    }
    song.reverb = (song.reverb & 0x80) != 0 ? song.reverb & 0x7f : layout.engine.reverb;
    foundSong = true;
    bankOffsets.insert(song.bankOffset);
    layout.songs.push_back(song);
  }
  if (layout.songs.empty()) {
    return std::nullopt;
  }
  addBanks(reader, layout, bankOffsets);
  return layout;
}

[[nodiscard]] std::optional<Mp2kLayout> readLayout(ByteReader reader, u32 settings) {
  const u32 value = reader.le32(settings);
  const u32 encodedRateIndex = (value >> 16) & 0x0f;
  const u32 rateIndex = encodedRateIndex == 0 ? kDefaultSampleRateIndex : encodedRateIndex;
  const u8 encodedMasterVolume = static_cast<u8>((value >> 12) & 0x0f);
  const u8 encodedDacMode = static_cast<u8>((value >> 20) & 0x0f);
  const u32 songLevels = reader.le32(settings + 4);
  const auto tableBase = romOffset(reader.le32(settings + 8), reader);
  if (!tableBase) {
    return std::nullopt;
  }
  const u64 table64 = static_cast<u64>(*tableBase) + static_cast<u64>(songLevels) * 12;
  if (table64 > reader.size()) {
    return std::nullopt;
  }

  Mp2kEngine engine{
      .settingsOffset = settings,
      .songTableOffset = static_cast<u32>(table64),
      .sampleRate = kSampleRates[rateIndex],
      .directSoundMasterVolume = encodedMasterVolume == 0 ? kDefaultDirectSoundMasterVolume : encodedMasterVolume,
      .dacBits = static_cast<u8>(encodedDacMode == 0 ? 8 : 17 - encodedDacMode),
      .reverb = static_cast<u8>(value & 0x7f)};
  const std::optional<PlayerTable> players =
      songLevels == 0 ? std::nullopt : std::optional<PlayerTable>{{.offset = *tableBase, .count = songLevels}};
  return readSongTable(reader, engine, players);
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
  if (!playerTable || !songTable || *songTable <= *playerTable || (*songTable - *playerTable) % 12 != 0) {
    return std::nullopt;
  }
  const u32 playerCount = (*songTable - *playerTable) / 12;
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
  for (u32 index = 0; index < 512 && reader.has(table + static_cast<u64>(index) * 8, 8); ++index) {
    const u32 raw = reader.le32(table + index * 8 + pointerWord * 4);
    if (raw == 0) {
      continue;
    }
    const auto offset = romOffset(raw, reader);
    Mp2kSong song;
    if (!offset || !validSong(reader, *offset, song)) {
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
  if (best.validSongs < 4) {
    return std::nullopt;
  }

  Mp2kLayout layout{.engine = {.songTableOffset = best.offset,
                               .sampleRate = kSampleRates[kDefaultSampleRateIndex],
                               .directSoundMasterVolume = kDefaultDirectSoundMasterVolume}};
  std::set<u32> bankOffsets;
  for (u32 index = 0; index < 4096 && reader.has(best.offset + static_cast<u64>(index) * 8, 8); ++index) {
    const u32 raw = reader.le32(best.offset + index * 8 + best.pointerWord * 4);
    if (raw == 0) {
      continue;
    }
    const auto offset = romOffset(raw, reader);
    Mp2kSong song{.index = index};
    if (!offset || !validSong(reader, *offset, song)) {
      break;
    }
    bankOffsets.insert(song.bankOffset);
    layout.songs.push_back(song);
  }
  addBanks(reader, layout, bankOffsets);
  return layout.songs.empty() ? std::nullopt : std::optional<Mp2kLayout>{std::move(layout)};
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

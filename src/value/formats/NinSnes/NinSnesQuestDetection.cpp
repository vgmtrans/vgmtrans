/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnesQuest.h"

#include "value/formats/NinSnes/NinSnesPatterns.h"

namespace vgmtrans::formats::nin_snes::quest {

using namespace core;

namespace {

// Recognize Tactics Ogre's command dispatch and BGM loader. Absolute operands
// remain relocatable; direct-page operands identify this driver revision.
const Pattern kTacticsOgreDispatch("\x68\xc8\x90\xb6\x68\xd8\xb0\x06\x68\xca\xb0\xc5\x2f\xda"
                                   "\x1c\x80\xa8\xb0\x5d\xe8\x0b\x2d\xe8\x0e\x2d\x1f\x88\x0b",
                                   "xxx?xxxxxxx?x?xxxxxx?xx?xx??", 28);
const Pattern kTacticsOgreSongList("\xe4\xbc\x9c\x1c\xfd\x8f\x81\x5d\xe5\xfa\x05\xe9\xfb\x05"
                                   "\xc4\x0c\xd8\x0d\xf7\x0c\xc4\x40\xfc\xf7\x0c\xc4\x41",
                                   "xxxxxxxxx??x??xxxxxxxxxxxxx", 27);
const Pattern kTacticsOgreInstrument("\x08\x80\x8d\x06\xcf\xda\x0c\x8d\x00\xf4\x28\x28\xd7", "xxxxxxxxxxxxx", 13);
const Pattern kTacticsOgreGate("\xf5\xfb\x20\x68\x07\xf0\x2b\x60\x84\x58\xfd\xf6\x7b\x21", "x??xxx?xxxxx??", 14);
const Pattern kTacticsOgreVelocity("\x28\x0f\x04\x59\xfd\x80\xb6\x8b\x21\x48\xff\xfd\xcf\xdd", "xxxxxxx??xxxxx", 14);
const Pattern kTacticsOgrePan("\xfd\xf6\xe0\x1e\xc4\x05\xf6\xcb\x1e\xeb\x04\xcf\xdb\x93", "xx??xxx??xxxxx", 14);
const Pattern kTacticsOgreDspInit("\xf5\xd8\x19\x30\x0b\xc4\xf2\xf5\xd9\x19\xc4\xf3\x3d\x3d\x2f\xf0", "x??xxxxx??xxxxxx", 16);

const Pattern kOgreBattleDispatch("\x68\xc8\x90\xa8\x68\xe0\xb0\x0a\x68\xca\xb0\xbe\x68\xc9\xf0\xd8"
                                  "\x2f\xe4\x1c\x80\xa8\xc0\x5d\xe8\x0b\x2d\xe8\xc2\x2d\x1f\x28\x0c",
                                  "xxx?xxx?xxx?xxx?x?xxxxxx?xx?xx??", 32);
const Pattern kOgreBattleInstrument("\x8f\x00\x06\x8d\x06\x68\xfc\x90\x0e\x80\xa8\xfc\xcf\x60\x88\xf8"
                                    "\xc4\x0c\x8f\x1d\x0d\x2f\x14\x8f\x1a\x0d\x8f\x00\x0c\xcf\x7a\x0c"
                                    "\xf8\x59\xf0\x05\xfc\xfc\x8f\x40\x06\xda\x0c",
                                    "xxxxxxxxxxxxxxx?xxx?xxxx?xx?xxxxxxxxxxxxxxx", 43);
const Pattern kOgreBattleSong("\xf5\x00\x34\xc4\x4e\xfd\xf5\xff\x33\xc4\x4d\x2f\x16\xc4\xf4\x28"
                              "\x7f\x1c\x5d\xe8\x80\xc5\x80\x01\xf5\x00\x1e\xc4\x4e\xfd\xf5\xff"
                              "\x1d\xc4\x4d",
                              "x??xxxx??xxx?xxxxxxxxxxxx??xxxx??xx", 35);
const Pattern kOgreBattleSfx("\xf4\xcd\xd4\xf5\x68\x80\x90\x0e\x1c\x5d\xf5\x00\x1f\xc4\x0a\xf5"
                             "\x01\x1f\xc4\x0b\x2f\x0d\x9c\x1c\x5d\xf5\x00\x1e\xc4\x0a\xf5\x01"
                             "\x1e\xc4\x0b",
                             "xxxxxxxxxxx??xxx??xxx?xxxx??xxx??xx", 35);
const Pattern kOgreBattleGate("\x68\x07\xf0\x1b\xfd\xf6\x6f\x17\xc4\x04",
                              "xxx?xx??xx", 10);
const Pattern kOgreBattleVelocity("\x28\x0f\xfd\xf6\x77\x17\xd5\x4a\x02\xd5\x02\x03",
                                  "xxxx??xxxxxx", 12);
const Pattern kOgreBattlePan("\x1c\x5d\xf5\x97\x17\x2d\xf5\x98\x17\xc4\x05",
                             "xxx??xx??xx", 11);
const Pattern kOgreBattleDir("\xe8\x18\xc4\x53\x8f\x5d\xf2\xc4\xf3",
                             "x?xxxxxxx", 9);

[[nodiscard]] std::vector<u8> table(ByteReader reader, u32 address, u32 count) {
  if (!reader.has(address, count)) {
    return {};
  }
  const auto bytes = reader.slice(address, count);
  return {bytes.begin(), bytes.end()};
}

// Ogre Battle needs more than a song-table probe: the captured song index
// selects between two BGM banks, and short pieces can use the SFX player.
// An SFX group starts a section directly and selects a different instrument bank
// and clock, so it must be identified before the sequence is decoded.
[[nodiscard]] std::optional<Layout> findOgreBattleLayout(ByteReader reader) {
  if (!kOgreBattleDispatch.find(reader)) {
    return std::nullopt;
  }
  const auto instrument = kOgreBattleInstrument.find(reader);
  const auto song = kOgreBattleSong.find(reader);
  const auto gate = kOgreBattleGate.find(reader);
  const auto velocity = kOgreBattleVelocity.find(reader);
  const auto pan = kOgreBattlePan.find(reader);
  const auto dir = kOgreBattleDir.find(reader);
  if (!instrument || !song || !gate || !velocity || !pan || !dir) {
    return std::nullopt;
  }
  Layout layout{.signature = Signature::Quest,
                .profile = ProfileId::QuestOgreBattle,
                .songIndex = reader.u8At(0xcc),
                .sectionPointerAddress = 0x4d,
                .instrumentTableAddress = reader.u8At(*instrument + 27) | (reader.u8At(*instrument + 24) << 8),
                .spcDirAddress = static_cast<u16>(reader.u8At(*dir + 1) << 8)};
  layout.questBuiltinInstruments = reader.u8At(*instrument + 15) | (reader.u8At(*instrument + 19) << 8);
  layout.durationRateTable = table(reader, reader.le16(*gate + 6), 8);
  layout.volumeTable = table(reader, reader.le16(*velocity + 4), 16);
  const auto pans = table(reader, reader.le16(*pan + 3), 42);
  if (layout.durationRateTable.size() != 8 || layout.volumeTable.size() != 16 || pans.size() != 42 ||
      reader.le16(*pan + 7) != reader.le16(*pan + 3) + 1) {
    return std::nullopt;
  }
  for (u8 side = 0; side < 2; ++side) {
    for (u8 i = 0; i < 21; ++i) {
      layout.questPanTable.push_back(pans[i * 2 + side]);
    }
  }
  // BGM requests are one-based and select one of two count-prefixed banks.
  // Use the captured current song; scanning the wrong bank can read score data.
  if (layout.songIndex != 0 && layout.songIndex != 0xff) {
    layout.songListAddress = reader.le16(*song + (layout.songIndex < 0x80 ? 1 : 25)) + 1;
    const u8 index = layout.songIndex & 0x7f;
    const u32 entry = layout.songListAddress + (index - 1) * 2;
    if (index != 0 && reader.has(entry, 2)) {
      layout.playlistAddress = reader.le16(entry);
      if (layout.playlistAddress >= 0x100 && isValidPlaylist(reader, layout)) {
        return layout;
      }
    }
    return std::nullopt;
  }
  // With no BGM selected, check for an active SFX group (e.g. Quest Logo).
  const auto sfx = kOgreBattleSfx.find(reader);
  if (sfx) {
    for (u8 group = 0; group < 2; ++group) {
      const u8 index = reader.u8At(0xcd + group);
      if (reader.u8At(0x181 + group) != 0x80 || index == 0 || index == 0xff) {
        continue;
      }
      layout.songListAddress = reader.le16(*sfx + (index < 0x80 ? 26 : 11));
      const u32 entry = layout.songListAddress + (index < 0x80 ? index - 1 : index & 0x7f) * 2;
      if (!reader.has(entry, 2)) {
        continue;
      }
      layout.playlistAddress = reader.le16(entry);
      if (layout.playlistAddress >= 0x100 && reader.has(layout.playlistAddress, 8) &&
          reader.le16(layout.playlistAddress) >= 0x100) {
        layout.songIndex = index;
        layout.questSfx = group + 1;
        layout.sectionTrackCount = 4;
        *layout.instrumentTableAddress += 0x200;
        return layout;
      }
    }
  }
  return std::nullopt;
}

// Tactics Ogre's BGM loader supplies the song-list pointer indirectly; separate
// routines locate the gate, velocity, pan, and sample-directory tables.
// Captures can precede song initialization, so prefer a valid pending request
// over the current song before falling back to other playlist candidates.
[[nodiscard]] std::optional<Layout> findTacticsOgreLayout(ByteReader reader) {
  const auto song = kTacticsOgreSongList.find(reader);
  const auto gate = kTacticsOgreGate.find(reader);
  const auto velocity = kTacticsOgreVelocity.find(reader);
  const auto pan = kTacticsOgrePan.find(reader);
  const auto dsp = kTacticsOgreDspInit.find(reader);
  if (!song || !gate || !velocity || !pan || !dsp) {
    return std::nullopt;
  }
  const u16 songPointer = reader.le16(*song + 9);
  if (!reader.has(songPointer, 2) || reader.le16(*song + 12) != songPointer + 1) {
    return std::nullopt;
  }
  Layout layout{.signature = Signature::Quest,
                .profile = ProfileId::QuestTacticsOgre,
                .songListAddress = reader.le16(songPointer),
                .sectionPointerAddress = 0x40,
                .instrumentTableAddress = 0x300};
  // The driver copies separate BGM/SFX tables. Read the BGM tables referenced by
  // the gate and velocity routines, not the adjacent SFX copies.
  layout.durationRateTable = table(reader, reader.le16(*gate + 12), 8);
  layout.volumeTable = table(reader, reader.le16(*velocity + 7), 16);
  layout.questPanTable = table(reader, reader.le16(*pan + 7), 21);
  const auto right = table(reader, reader.le16(*pan + 2), 21);
  layout.questPanTable.insert(layout.questPanTable.end(), right.begin(), right.end());
  // DSP registers are initialized from register/value pairs. Read DIR from
  // that list to locate the sample directory.
  const u16 registers = reader.le16(*dsp + 1);
  if (reader.le16(*dsp + 8) != registers + 1) {
    return std::nullopt;
  }
  for (u32 i = 0; i < 0x80 && reader.has(registers + i, 2); i += 2) {
    const u8 reg = reader.u8At(registers + i);
    if (reg >= 0x80) {
      break;
    }
    if (reg == 0x5d) {
      layout.spcDirAddress = static_cast<u16>(reader.u8At(registers + i + 1) << 8);
    }
  }
  if (layout.durationRateTable.size() != 8 || layout.volumeTable.size() != 16 || layout.questPanTable.size() != 42 ||
      !layout.spcDirAddress) {
    return std::nullopt;
  }
  // Requests 1-15 select (song-1)*2; raw input ports also contain the driver's
  // handshake/control traffic.
  std::vector<u8> candidates;
  for (u8 index : {reader.u8At(0xb9), reader.u8At(0xbc), reader.u8At(0xf4)}) {
    if (index > 0 && index < 0x10) {
      candidates.push_back(index);
    }
  }
  for (u8 index = 1; index < 0x10; ++index) {
    candidates.push_back(index);
  }
  for (const u8 index : candidates) {
    const u32 entry = layout.songListAddress + (index - 1) * 2;
    if (!reader.has(entry, 2)) {
      continue;
    }
    layout.songIndex = index;
    layout.playlistAddress = reader.le16(entry);
    if (layout.playlistAddress >= 0x100 && isValidPlaylist(reader, layout)) {
      return layout;
    }
  }
  return std::nullopt;
}

}  // namespace

// Quest keeps N-SPC playlists but changes the dispatch and song-loader code
// recognized by the shared detector. Each revision needs a complete layout
// probe: driver recognition, playback tables, instruments, and song selection.
// Other variants share most of these steps in NinSnesLayout.cpp; Quest still
// uses the shared playlist validator for its BGM candidates.
std::optional<Layout> findLayout(ByteReader reader) {
  if (!kTacticsOgreDispatch.find(reader) || !kTacticsOgreInstrument.find(reader)) {
    return findOgreBattleLayout(reader);
  }
  return findTacticsOgreLayout(reader);
}

}  // namespace vgmtrans::formats::nin_snes::quest

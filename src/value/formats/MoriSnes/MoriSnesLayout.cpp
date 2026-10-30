/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MoriSnes/MoriSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <utility>

namespace vgmtrans::formats::mori_snes {

using namespace core;

namespace {

// Gokinjo Bouken Tai $0c3c. The two absolute indexed loads expose the song
// pointer table; the surrounding parser distinguishes a real header from data.
constexpr auto kLoadSequence = makeMaskedBytePattern(
    "\x1c\xfd\xf6\x00\x00\xc4\x04\xf6\x00\x00\xc4\x05\x8d\x00\xf7\x04\x10\x05\x68\xff\xd0\x00\x6f",
    "xxx??xxx??xxxxxxxxxxx?x");
constexpr auto kSetDir = makeMaskedBytePattern("\x8f\x00\xf3\x8f\x6c\xf2", "x?xxxx");
constexpr auto kSetDirShien = makeMaskedBytePattern(
    "\xe8\x00\x8d\x5d\xcb\xf2\xc4\xf3\xe8\x00\x8d\x6c\xcb\xf2\xc4\xf3",
    "x?xxxxxxx?xxxxxx");

// Preset commands E2-E5 use one byte table. The paired indexed loads in E2
// establish both the base and its adjacent fine-pitch table.
constexpr auto kLoadPresetPitch =
    makeMaskedBytePattern("\x6d\xf7\x2d\xfd\xf6\x00\x00\xd5\x7e\x02\xf6\x00\x00", "xxxxx??xxxx??");
constexpr auto kLoadPresetPitchShien = makeMaskedBytePattern(
    "\x8d\x00\xf7\x29\x3a\x29\xfd\xf6\x00\x00\xd5\x7e\x02\xf6\x00\x00\x2f\x07",
    "xxxxxxxx??xxxx??xx");

// The mixer indexes a 33-entry equal-power-ish table from opposite ends.
constexpr auto kLoadPan = makeMaskedBytePattern(
    "\xf5\x0a\x02\xfd\xf6\x00\x00\xee\x6d\xcf\x7d\x9f\xc4\xf2", "xxxxx??xxxxxxx");
// Shin Nekketsu uses the standard 33-step pan table and command dialect, but
// writes each channel gain directly instead of leaving it on the stack.
constexpr auto kLoadPanShinNekketsu = makeMaskedBytePattern(
    "\xf5\x0a\x02\xfd\xf6\x00\x00\xeb\x00\xcf\x7d\x9f\xc4\xf2\xcb\xf3\x2d\xe8\x20\x80\xb5\x0a\x02\xfd\xf6\x00\x00",
    "xxxxx??x?xxxxxxxxxxxxxxxx??");
constexpr auto kLoadPanCbChara = makeMaskedBytePattern(
    "\xf5\x40\x02\xfd\xf6\x00\x00\xeb\x00\xcf\xdd\xee\xcb\xf2\xc4\xf3\xfc\x6d\xe8\x14\x80\xb5\x40\x02\xfd\xf6\x00\x00",
    "xxxxx??x?xxxxxxxxxxxxxxxxx??");
constexpr auto kLoadPanShien = makeMaskedBytePattern(
    "\xf5\x0a\x02\xfd\xf6\x00\x00\xeb\x31\xcf\xdd\xee\xcb\xf2\xc4\xf3\x6d\xe8\x14\x80\xb5\x0a\x02\xfd\xf6\x00\x00",
    "xxxxx??xxxxxxxxxxxxxxxxxx??");

// All three forms write the captured immediate to Timer 0, then enable it.
constexpr auto kSetTimer0Immediate = makeMaskedBytePattern("\x8f\x00\xfa\x8f\x01\xf1\x6f", "x?xxxxx");
constexpr auto kSetTimer0Direct =
    makeMaskedBytePattern("\xe8\x00\xc4\xfa\xe8\x01\xc4\xf1\x6f", "x?xxxxxxx");
constexpr auto kSetTimer0Absolute =
    makeMaskedBytePattern("\xe8\x00\xc5\xfa\x00\xe8\x01\xc5\xf1\x00\x8d\x64", "x?xxxxxxxxxx");

[[nodiscard]] u16 relativeTarget(u16 continuation, s16 relative) {
  return static_cast<u16>(continuation + relative);
}

[[nodiscard]] std::optional<std::vector<TrackHeader>> readHeader(ByteReader reader, u16 address) {
  std::vector<TrackHeader> tracks;
  std::set<u8> channels;
  u32 cursor = address;
  for (u32 count = 0; count < kTrackCount; ++count) {
    if (!reader.has(cursor, 1)) {
      return std::nullopt;
    }
    const u8 channel = reader.u8At(cursor);
    if (channel == 0xff) {
      return tracks.empty() ? std::nullopt : std::optional{tracks};
    }
    if (channel >= kTrackCount || !channels.insert(channel).second || !reader.has(cursor + 1, 2)) {
      return std::nullopt;
    }
    const u16 continuation = static_cast<u16>(cursor + 3);
    const u16 start = relativeTarget(continuation, static_cast<s16>(reader.le16(cursor + 1)));
    if (!reader.has(start, 1)) {
      return std::nullopt;
    }
    tracks.push_back(TrackHeader{.channel = channel, .startAddress = start, .range = reader.range(cursor, 3)});
    cursor += 3;
  }
  return reader.has(cursor, 1) && reader.u8At(cursor) == 0xff ? std::optional{tracks} : std::nullopt;
}

struct SelectedHeader {
  u8 index;
  u16 address;
  std::vector<TrackHeader> tracks;
  std::vector<SfxVoice> sfxVoices;
};

[[nodiscard]] std::optional<std::vector<SfxVoice>> readSfxHeader(ByteReader reader, u16 address) {
  if (!reader.has(address, 2) || reader.u8At(address) < 0x80 || reader.u8At(address) == 0xff) {
    return std::nullopt;
  }
  std::vector<SfxVoice> voices;
  u16 cursor = static_cast<u16>(address + 1);
  while (voices.size() < 8 && reader.has(cursor, 1)) {
    if (reader.u8At(cursor) == 0xff) {
      return voices.empty() ? std::nullopt : std::optional{voices};
    }
    if (!reader.has(cursor, 3)) {
      return std::nullopt;
    }
    const u16 script = relativeTarget(static_cast<u16>(cursor + 3), static_cast<s16>(reader.le16(cursor + 1)));
    if (!reader.has(script, 1)) {
      return std::nullopt;
    }
    voices.push_back(SfxVoice{.scriptAddress = script, .range = reader.range(cursor, 3)});
    cursor = static_cast<u16>(cursor + 3);
  }
  if (!voices.empty() && reader.has(cursor, 1) && reader.u8At(cursor) == 0xff) {
    return voices;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<SelectedHeader> selectHeader(ByteReader reader, u16 list, u16 trackSongIndices) {
  const auto music = [&](u8 index) -> std::optional<SelectedHeader> {
    const u32 pointer = list + index * 2u;
    if (!reader.has(pointer, 2)) {
      return std::nullopt;
    }
    const u16 header = reader.le16(pointer);
    if (header == 0 || header == 0xffff) {
      return std::nullopt;
    }
    if (auto tracks = readHeader(reader, header)) {
      return SelectedHeader{.index = index, .address = header, .tracks = std::move(*tracks)};
    }
    return std::nullopt;
  };

  const auto sfx = [&](u8 index) -> std::optional<SelectedHeader> {
    const u32 pointer = list + index * 2u;
    if (!reader.has(pointer, 2)) {
      return std::nullopt;
    }
    const u16 header = reader.le16(pointer);
    if (auto voices = readSfxHeader(reader, header)) {
      return SelectedHeader{.index = index, .address = header, .sfxVoices = std::move(*voices)};
    }
    return std::nullopt;
  };

  // A nonzero CPU port contains a song command which the dump captured just
  // before the driver consumed it. This is how a few short fanfare SPCs retain
  // an older song in slot one while requesting their real slot in F4.
  if (reader.has(0xf4, 1)) {
    const u8 pending = reader.u8At(0xf4);
    if (pending != 0 && pending < 0x80) {
      if (auto selected = music(pending)) {
        return selected;
      }
      if (auto selected = sfx(pending)) {
        return selected;
      }
    }
  }

  // Live source tracks retain the table index which started them. Prefer that
  // evidence over the conventional slot-one fallback.
  if (reader.has(trackSongIndices, kTrackCount)) {
    std::array<u8, 128> counts{};
    for (u32 track = 0; track < kTrackCount; ++track) {
      const u8 index = reader.u8At(trackSongIndices + track);
      if (index < 0x80) {
        ++counts[index];
      }
    }
    const auto most = std::max_element(counts.begin(), counts.end());
    if (*most != 0) {
      const u8 index = static_cast<u8>(std::distance(counts.begin(), most));
      if (auto selected = music(index)) {
        return selected;
      }
    }
  }

  // Hardware-only sound effects leave their command index in $38 even after
  // their last voice has ended, while source-track identifiers remain empty.
  if (reader.has(0x38, 1)) {
    const u8 index = reader.u8At(0x38);
    if (index < 0x80) {
      if (auto selected = sfx(index)) {
        return selected;
      }
    }
  }

  // Mori's music table conventionally reserves entry zero.
  if (auto selected = music(1)) {
    return selected;
  }
  for (u16 index = 0; index < 128; ++index) {
    if (index != 1) {
      if (auto selected = music(static_cast<u8>(index))) {
        return selected;
      }
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto loader = findBytePattern(reader, kLoadSequence);
  if (!loader) {
    return std::nullopt;
  }

  Version version;
  std::optional<u32> dir;
  std::optional<u32> presets;
  std::optional<u32> pan;
  std::optional<u32> timerInit;
  const auto standardDir = findBytePattern(reader, kSetDir);
  const auto standardPresets = findBytePattern(reader, kLoadPresetPitch);
  const auto standardPan = findBytePattern(reader, kLoadPan);
  const auto shinNekketsuPan = findBytePattern(reader, kLoadPanShinNekketsu);
  const auto sparseDir = findBytePattern(reader, kSetDirShien);
  const auto cbCharaPan = findBytePattern(reader, kLoadPanCbChara);
  if (standardDir && standardPresets && (standardPan || shinNekketsuPan)) {
    version = shinNekketsuPan ? Version::ShinNekketsu : Version::Gokinjo;
    dir = standardDir;
    presets = standardPresets;
    pan = shinNekketsuPan ? shinNekketsuPan : standardPan;
    timerInit = findBytePattern(reader, kSetTimer0Immediate);
  } else if (sparseDir && cbCharaPan) {
    timerInit = findBytePattern(reader, kSetTimer0Absolute);
    version = timerInit && reader.u8At(*timerInit + 1) == 0x28 ? Version::Combatribes : Version::CbChara;
    dir = sparseDir;
    pan = cbCharaPan;
  } else {
    dir = sparseDir;
    presets = findBytePattern(reader, kLoadPresetPitchShien);
    pan = findBytePattern(reader, kLoadPanShien);
    if (!dir || !presets || !pan) {
      return std::nullopt;
    }
    version = Version::Shien;
    timerInit = findBytePattern(reader, kSetTimer0Direct);
  }
  if (!timerInit) {
    return std::nullopt;
  }
  const DriverTraits traits = driverTraits(version, reader.u8At(*timerInit + 1));
  const bool early = usesEarlySparseDriver(version);

  const u16 songList = reader.le16(*loader + 3);
  if (reader.le16(*loader + 8) != static_cast<u16>(songList + 1)) {
    return std::nullopt;
  }
  auto selected = selectHeader(reader, songList, traits.trackSongIndexAddress);
  if (!selected) {
    return std::nullopt;
  }

  const u16 presetTable = presets ? reader.le16(*presets + (version == Version::Shien ? 8u : 5u)) : 0;
  const u16 presetPitchHigh = presets ? reader.le16(*presets + (version == Version::Shien ? 14u : 11u)) : 0;
  const u16 panTable = reader.le16(*pan + 5);
  const u16 directory = static_cast<u16>(reader.u8At(*dir + 1) << 8);
  if ((!usesSparseCommands(version) && presetPitchHigh != static_cast<u16>(presetTable + 4)) ||
      (version == Version::ShinNekketsu && reader.le16(*pan + 25) != panTable) ||
      (early && reader.le16(*pan + 26) != panTable) ||
      (!early && (!reader.has(presetTable, 1) || !reader.has(presetPitchHigh, 1))) ||
      !reader.has(panTable, traits.maximumPan + 1u) || !reader.has(directory, 4)) {
    return std::nullopt;
  }

  return Layout{
      .traits = traits,
      .songListAddress = songList,
      .songHeaderAddress = selected->address,
      .spcDirAddress = directory,
      .presetTableAddress = presetTable,
      .presetPitchHighAddress = presetPitchHigh,
      .panTableAddress = panTable,
      .songIndex = selected->index,
      .tracks = std::move(selected->tracks),
      .sfxVoices = std::move(selected->sfxVoices),
  };
}

}  // namespace vgmtrans::formats::mori_snes

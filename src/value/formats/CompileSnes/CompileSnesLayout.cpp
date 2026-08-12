/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CompileSnes/CompileSnes.h"

#include "value/scan/BytePattern.h"

#include <array>
#include <map>

namespace vgmtrans::formats::compile_snes {

using namespace core;

namespace {

// Shared by all seven known driver builds. Wildcards cover the relocated
// engine header and the early ($4e) / late ($50) direct-page work area.
constexpr auto kLoadEngineHeader =
    makeMaskedBytePattern("\xe5\x00\x00\xc4\x00\xe5\x00\x00\xc4\x00\xe5\x00\x00\xc4\x00\x60"
                          "\x88\x11\xc4\x00\xe5\x00\x00\xc4\x00\x88\x00\xc4\x00",
                          "x??x?x??x?x??x?xxxx?x??x?xxx?");

constexpr std::array<u8, 16> kRegularPitchPrefix{
    0x12, 0x00, 0x13, 0x00, 0x14, 0x00, 0x15, 0x00, 0x17, 0x00, 0x18, 0x00, 0x19, 0x00, 0x1b, 0x00,
};

[[nodiscard]] bool validSong(ByteReader reader, u16 songList, u8 song, u16* header = nullptr) {
  const u32 pointer = songList + song * 2u;
  if (!reader.has(pointer, 2)) {
    return false;
  }
  const u16 address = reader.le16(pointer);
  if (!reader.has(address, 1)) {
    return false;
  }
  const u8 tracks = reader.u8At(address);
  if (tracks == 0 || tracks > 16 || !reader.has(address, 1 + tracks * 14u)) {
    return false;
  }
  for (u32 track = 0; track < tracks; ++track) {
    if (!reader.has(reader.le16(address + 1 + track * 14u + 8), 1)) {
      return false;
    }
  }
  if (header != nullptr) {
    *header = address;
  }
  return true;
}

[[nodiscard]] std::optional<std::pair<u8, u16>> activeSong(ByteReader reader, u16 songList, bool early) {
  const u16 songState = early ? 0x044a : 0x01f0;
  std::map<u8, unsigned> votes;
  for (u32 track = 0; track < 16; ++track) {
    if (reader.has(0x80 + track, 1) && reader.u8At(0x80 + track) != 0 && reader.has(songState + track, 1)) {
      const u8 song = reader.u8At(songState + track);
      if (validSong(reader, songList, song)) {
        ++votes[song];
      }
    }
  }
  std::optional<std::pair<u8, u16>> result;
  unsigned best = 0;
  for (const auto [song, count] : votes) {
    u16 header = 0;
    if (count > best && validSong(reader, songList, song, &header)) {
      result = std::pair{song, header};
      best = count;
    }
  }
  if (result) {
    return result;
  }
  for (u32 song = 0; song < 256; ++song) {
    u16 header = 0;
    if (validSong(reader, songList, static_cast<u8>(song), &header)) {
      return std::pair{static_cast<u8>(song), header};
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto code = findBytePattern(reader, kLoadEngineHeader);
  if (!code || !reader.has(*code + 1, 2)) {
    return std::nullopt;
  }
  const u16 engine = reader.le16(*code + 1);
  if (engine < 0x0100 || !reader.has(engine, 0x18)) {
    return std::nullopt;
  }

  const bool early = reader.u8At(*code + 4) == 0x4e;
  const bool maskedSong = *code >= 4 && reader.u8At(*code - 4) == 0x28 && reader.u8At(*code - 3) == 0x0c;
  const Version version = early ? (maskedSong ? Version::JakiCrush : Version::Aleste)
                                : (maskedSong ? Version::SuperPuyo : Version::Standard);
  const u16 songList = reader.le16(engine);
  const auto song = activeSong(reader, songList, early);
  if (!song) {
    return std::nullopt;
  }

  const u16 duration = reader.le16(engine + 2);
  if (!reader.has(duration, 0x11 + 30 * 8u)) {
    return std::nullopt;
  }
  u16 regularPitch = 0;
  for (u32 offset = 2; offset + kRegularPitchPrefix.size() <= reader.size(); ++offset) {
    if (matchesBytes(reader, offset, kRegularPitchPrefix)) {
      regularPitch = static_cast<u16>(offset - 2);
      break;
    }
  }
  if (regularPitch == 0) {
    return std::nullopt;
  }

  return Layout{
      .version = version,
      .engineHeaderAddress = engine,
      .songListAddress = songList,
      .songHeaderAddress = song->second,
      .songIndex = song->first,
      .durationTableAddress = duration,
      .percussionTableAddress = static_cast<u16>(duration + 0x11),
      .volumeEnvelopeTableAddress = reader.le16(engine + 4),
      .vibratoTableAddress = reader.le16(engine + 6),
      .gainEnvelopeTableAddress = reader.le16(engine + 0x0a),
      .adsrTableAddress = reader.le16(engine + 0x0c),
      .echoPresetTableAddress = reader.le16(engine + 0x10),
      .tuningTableAddress = reader.le16(engine + 0x12),
      .panEnvelopeTableAddress = reader.le16(engine + 0x14),
      .pitchTableListAddress = early ? u16{0} : reader.le16(engine + 0x16),
      .regularPitchTableAddress = regularPitch,
      .spcDirAddress = static_cast<u16>(reader.u8At(engine + 0x0e) << 8),
      .globalTranspose = static_cast<s8>(reader.u8At(engine + 0x0f)),
  };
}

}  // namespace vgmtrans::formats::compile_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"
#include "value/scan/BytePattern.h"

namespace vgmtrans::formats::sculpt_soft_snes {

using namespace core;

namespace {

// Bugs Bunny $0e8e: select a pointer table, then index it with a 9-bit offset.
constexpr auto kLookup = makeMaskedBytePattern(
    "\x8f\x00\x0b\x1c\x2b\x0b\x60\x96\x00\x00\xc4\x0a\xe4\x0b\x96\x00\x00\xc4\x0b", "xx?xx?xx??x?x?x??x?");
constexpr auto kSong =
    makeMaskedBytePattern("\x8d\x1a\x3f\x00\x00\xda\x6d\x8d\x00\xf7\x6d\xc4\x60\xc4\x61", "xxx??x?xxx?x?x?");
// This revision uses F6 phrase descriptors and a 16-bit F1 volume multiplier.
// Earlier and later Sculptured engines have different command maps.
constexpr auto kPhrase =
    makeMaskedBytePattern("\xf8\x62\xf4\x7f\x80\xa8\x05\xd4\x7f\x60\x94\x87\xfd\xcb\x00\xf8\x63", "x?x?xxxx?xx?xx?x?");
constexpr auto kDispatch = makeMaskedBytePattern("\xdd\x28\x0f\x1c\x5d\x1f\x00\x00", "xxxxxx??");
constexpr auto kPitch = makeMaskedBytePattern("\xf6\x00\x00\xc4\x2c\xf6\x00\x00\xc4\x2d\xe8\x04\x80", "x??x?x??x?xxx");
// NHL Stanley Cup and Rocko add six octaves before the lookup and shift over
// ten octaves. Their FB/FC commands extend the envelope gate and suppress key-on.
constexpr auto kExtendedPitch =
    makeMaskedBytePattern("\xf6\x00\x00\xc4\x2f\xf6\x00\x00\xc4\x30\xe8\x0a\x80", "x??x?x??x?xxx");
constexpr auto kPitchBias = makeMaskedBytePattern("\x60\x88\xa0\x5d\xdd\x88\x05\xfd\x7d\xad\x0a", "xxxxxxxxxxx");
constexpr auto kLongGate =
    makeMaskedBytePattern("\xe8\x00\xfd\xda\x3d\x8f\x01\xce\x3f\x00\x00\x2d\xfd\xf8\x65", "xxxx?xx?x??xxx?");
constexpr auto kLegato = makeMaskedBytePattern("\x8f\x01\xcf\x5f\x00\x00", "xx?x??");
constexpr auto kDelta = makeMaskedBytePattern("\x96\x00\x00\xd4\xab\xf4\xac\x96\x00\x00\xd4\xac", "x??x?x?x??x?");
constexpr auto kDirectory = makeMaskedBytePattern("\x8f\x00\x10\x8f\x1a\x11\x8d\x03", "x??x??xx");

constexpr auto kTimer = makeMaskedBytePattern("\x8f\x20\xfa\x8f\x81\xf1", "x?xxxx");
constexpr auto kFrame =
    makeMaskedBytePattern("\xe4\xfd\x60\x84\x19\xc4\x19\x68\x05\x90\x00\xa8\x05\xc4\x19", "xxxx?x?x?x?x?x?");

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto lookup = findBytePattern(reader, kLookup);
  const auto songCode = findBytePattern(reader, kSong);
  const auto phrase = findBytePattern(reader, kPhrase);
  const auto dispatch = findBytePattern(reader, kDispatch);
  auto pitch = findBytePattern(reader, kPitch);
  Revision revision = Revision::Standard;
  if (!pitch) {
    pitch = findBytePattern(reader, kExtendedPitch);
    revision = Revision::Extended;
  }
  const auto delta = findBytePattern(reader, kDelta);
  const auto directory = findBytePattern(reader, kDirectory);
  const auto timer = findBytePattern(reader, kTimer);
  const auto frame = findBytePattern(reader, kFrame);
  if (!lookup || !songCode || !phrase || !dispatch || !pitch || !delta || !directory || !timer || !frame ||
      reader.le16(*songCode + 3) != *lookup || reader.u8At(*frame + 8) == 0 ||
      reader.u8At(*frame + 8) != reader.u8At(*frame + 12)) {
    return std::nullopt;
  }
  const u16 commands = reader.le16(*dispatch + 6);
  const u16 tables = reader.le16(*lookup + 8);
  const u16 pitchTable = reader.le16(*pitch + 1);
  const u16 deltaTable = reader.le16(*delta + 1);
  if (revision == Revision::Extended &&
      (*pitch < 57 || !matchesBytePattern(reader, *pitch - 57, kPitchBias) || !reader.has(commands, 26) ||
       !matchesBytePattern(reader, reader.le16(commands + 22), kLongGate) ||
       !matchesBytePattern(reader, reader.le16(commands + 24), kLegato))) {
    return std::nullopt;
  }
  if (!reader.has(commands, 22) || reader.le16(commands + 12) != *phrase ||
      reader.le16(commands + 14) != reader.le16(commands + 16) || !reader.has(tables, 32) ||
      reader.le16(*lookup + 15) != tables + 1 || reader.le16(*pitch + 6) != pitchTable + 240 ||
      reader.le16(*delta + 8) != deltaTable + 32 || !reader.has(pitchTable, 480) || !reader.has(deltaTable, 64)) {
    return std::nullopt;
  }
  const u16 song = reader.le16(reader.u8At(*songCode + 6));
  if (song < 0x200 || !reader.has(song, 1)) {
    return std::nullopt;
  }
  const u8 tracks = reader.u8At(song);
  if (tracks == 0 || tracks > 8 || !reader.has(song, 1u + tracks)) {
    return std::nullopt;
  }
  const u16 trackTable = reader.le16(tables + 0x10);
  for (u32 track = 0; track < tracks; ++track) {
    const u32 entry = trackTable + reader.u8At(song + 1 + track) * 2u;
    if (!reader.has(entry, 2) || reader.le16(entry) < 0x200 || reader.le16(entry) == 0xffff) {
      return std::nullopt;
    }
  }
  return Layout{
      .revision = revision,
      .tables = tables,
      .directory = static_cast<u16>(reader.u8At(*directory + 1) | (reader.u8At(*directory + 4) << 8)),
      .song = song,
      .pitchTable = pitchTable,
      .deltaTable = deltaTable,
      .frameMicroseconds =
          125u * (reader.u8At(*timer + 1) == 0 ? 256u : reader.u8At(*timer + 1)) * reader.u8At(*frame + 8),
      .tracks = tracks,
  };
}

}  // namespace vgmtrans::formats::sculpt_soft_snes

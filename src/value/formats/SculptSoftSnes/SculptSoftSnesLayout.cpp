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
// Early tracks call pattern lists, which in turn call note patterns.
constexpr auto kEarlyList = makeMaskedBytePattern(
    "\x3f\x00\x00\x8d\x16\x3f\x00\x00\xf8\x5e\xd4\x7b\xdd\xd4\x7c\xf8\x5d\xe8\x0a\xd4\x61\x5f\x00\x00",
    "x??xxx??x?x?xx?x?xxx?x??");
constexpr auto kEarlyDispatch = makeMaskedBytePattern("\x3f\x00\x00\x5d\x1f\x00\x00", "x??xx??");
constexpr auto kEarlyPattern =
    makeMaskedBytePattern("\x8d\x10\x3f\x00\x00\xe8\x06\xd4\x61\x5f\x00\x00", "xxx??xxx?x??");
constexpr auto kEarlyPatternEnd = makeMaskedBytePattern("\xf8\x5d\xe8\x0a\xd4\x61\x5f\x00\x00", "x?xxx?x??");
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

constexpr auto kLateDispatch =
    makeMaskedBytePattern("\xdd\x28\x1f\x1c\xfd\xf6\x00\x00\xc5\x00\x00\xf6\x00\x00\xc5\x00\x00", "xxxxxx??x??x??x??");
constexpr auto kLateTimers = makeMaskedBytePattern(
    "\x8f\xa0\xfa\x8f\xa0\xfb\x8f\x20\xfc\x8f\x87\xf1\x3f\x00\x00\x8f\x00\x25\x8f\x1f\x26", "x?xx?xx?xxxxx??x??x??");
constexpr auto kLateAbsolute = makeMaskedBytePattern("\x3f\x00\x00\xfd\x3f\x00\x00\x2d\xe8\x00", "x??xx??xxx");
constexpr auto kLateInlineTracks =
    makeMaskedBytePattern("\xe4\x88\x5d\x1c\xbc\xfd\xf7\x8a\xd4\x8c\xfc\xf7\x8a\xd4\xa0", "x?xxxxx?x?xx?x?");
constexpr auto kLateIndexedTracks =
    makeMaskedBytePattern("\xeb\x87\xfc\xf7\x89\x8d\x10\x3f\x00\x00\xf8\x87\xd4\x8b\xdd\xd4\x9f", "x?xx?xxx??x?x?xx?");

constexpr auto kLatePhraseTranspose =
    makeMaskedBytePattern("\xdd\x60\x95\x00\x00\xd5\x00\x00\xae\x95\x00\x00\xd5\x00\x00", "xxx??x??xx??x??");
constexpr auto kLateTrackTranspose =
    makeMaskedBytePattern("\x3f\x00\x00\xd5\x00\x00\x3f\x00\x00\xd5\x00\x00\x2f", "x??x??x??x??x");

// Host commands use parallel rings for the command and its two arguments.
// Some revisions also mirror the dequeued command into the communication ports.
constexpr auto kHostQueue =
    makeMaskedBytePattern("\xe4\xe8\xf0\x2f\x9c\xc4\xe8\xf8\xea\xf5\x50\x10\x28\xfe\xc4\x31", "x?x?xx?x?x??xxx?");
constexpr auto kQueueArguments = makeMaskedBytePattern("\xf5\x60\x10\xc4\x32\xf5\x70\x10\xc4\x33", "x??x?x??x?");
constexpr auto kQueuePortArguments =
    makeMaskedBytePattern("\xc4\xf7\xf5\x04\xfd\xc4\x31\xc4\xf4\xf5\x1c\xfd\xc4\xf5\xc4\x32", "xxx??x?xxx??xxx?");
constexpr auto kQueueDispatch = makeMaskedBytePattern(
    "\x3d\xc8\x10\xd0\x02\xcd\x00\xd8\xea\xe4\x31\x5d\x28\xfe\xc8\x49\xb0\x07\xe4\x32\xeb\x33\x3f\x96\x0b",
    "xx?xxxxx?x?xxxx?x?x?x?x??");
constexpr auto kStartSong = makeMaskedBytePattern("\x2d\x3f\x00\x00\xae\x92\x07", "xx??xx?");

std::optional<u8> queuedSongIndex(ByteReader reader, u32 songCode) {
  const auto queue = findBytePattern(reader, kHostQueue);
  if (!queue || songCode < 7 || !matchesBytePattern(reader, songCode - 7, kStartSong)) {
    return std::nullopt;
  }
  const u32 arguments = *queue + 16;
  const bool portMirror = matchesBytePattern(reader, arguments, kQueuePortArguments);
  if (!portMirror && !matchesBytePattern(reader, arguments, kQueueArguments)) {
    return std::nullopt;
  }
  const u32 dispatch = arguments + (portMirror ? 16 : 10);
  if (!matchesBytePattern(reader, dispatch, kQueueDispatch) || reader.u8At(*queue + 1) != reader.u8At(*queue + 6) ||
      reader.u8At(*queue + 8) != reader.u8At(dispatch + 8) || reader.u8At(*queue + 15) != reader.u8At(dispatch + 10)) {
    return std::nullopt;
  }
  const u16 jump = reader.le16(dispatch + 23);
  if (!reader.has(jump, 3) || reader.u8At(jump) != 0x1f) {
    return std::nullopt;
  }
  const u16 commands = reader.le16(jump + 1);
  if (!reader.has(commands, 8)) {
    return std::nullopt;
  }
  const u16 startSong = reader.le16(commands + 6);
  if (!reader.has(startSong, 3) || reader.u8At(startSong) != 0x3f || reader.le16(startSong + 1) != songCode - 7) {
    return std::nullopt;
  }
  const u8 capacity = reader.u8At(dispatch + 2);
  const u8 count = reader.u8At(reader.u8At(*queue + 1));
  const u8 head = reader.u8At(reader.u8At(*queue + 8));
  const u16 commandRing = reader.le16(*queue + 10);
  const u16 argumentRing = reader.le16(arguments + (portMirror ? 3 : 1));
  if (capacity == 0 || count > capacity || head >= capacity || !reader.has(commandRing, capacity) ||
      !reader.has(argumentRing, capacity)) {
    return std::nullopt;
  }
  std::optional<u8> song;
  for (u32 i = 0; i < count; ++i) {
    const u32 slot = (head + i) % capacity;
    if ((reader.u8At(commandRing + slot) & 0xfe) == 6) {
      song = reader.u8At(argumentRing + slot);
    }
  }
  return song;
}

std::optional<Layout> findLateLayout(ByteReader reader, u32 lookup, u32 songCode, u32 pitch) {
  const auto dispatch = findBytePattern(reader, kLateDispatch);
  const auto timers = findBytePattern(reader, kLateTimers);
  if (!dispatch || !timers || !reader.has(songCode, 22) || lookup < 3 || reader.le16(songCode + 3) != lookup - 3 ||
      pitch < 57 || !matchesBytePattern(reader, pitch - 57, kPitchBias)) {
    return std::nullopt;
  }
  const u16 commands = reader.le16(*dispatch + 6);
  const u16 tables = reader.le16(lookup + 8);
  const u16 pitchTable = reader.le16(pitch + 1);
  if (!reader.has(commands, 64) || !reader.has(tables, 28) || !reader.has(pitchTable, 480) ||
      reader.le16(lookup + 15) != tables + 1 || reader.le16(pitch + 6) != pitchTable + 240 ||
      reader.le16(*dispatch + 12) != commands + 1 ||
      !matchesBytePattern(reader, reader.le16(commands + 30), kLateAbsolute)) {
    return std::nullopt;
  }
  for (const auto& [opcode, duration] :
       std::array<std::pair<u8, u8>, 4>{{{0xf8, 32}, {0xfd, 64}, {0xfe, 128}, {0xff, 0}}}) {
    const u16 handler = reader.le16(commands + 2 * (opcode - 0xe0));
    if (!reader.has(handler, 4) || reader.u8At(handler) != 0xe8 || reader.u8At(handler + 1) != duration ||
        reader.u8At(handler + 2) != 0x2f) {
      return std::nullopt;
    }
  }
  const u16 phrase = reader.le16(commands + 44);
  const u16 transpose = reader.le16(commands + 46);
  if (!matchesBytePattern(reader, phrase + 0x32u, kLatePhraseTranspose) ||
      !matchesBytePattern(reader, transpose, kLateTrackTranspose)) {
    return std::nullopt;
  }
  u16 song = reader.le16(reader.u8At(songCode + 6));
  if (const auto index = queuedSongIndex(reader, songCode)) {
    const u16 songs = reader.le16(tables + 0x1a);
    if (songs < 0x200) {
      return std::nullopt;
    }
    const u32 slot = songs + 2u * *index;
    song = reader.has(slot, 2) ? reader.le16(slot) : 0;
  }
  if (song < 0x200 || !reader.has(song, 1)) {
    return std::nullopt;
  }
  const u8 tracks = reader.u8At(song);
  const u16 trackInit = reader.le16(songCode + 20);
  const bool inlinePointers = matchesBytePattern(reader, trackInit, kLateInlineTracks);
  if (!inlinePointers &&
      (!matchesBytePattern(reader, trackInit, kLateIndexedTracks) || reader.le16(trackInit + 8) != lookup - 3)) {
    return std::nullopt;
  }
  if (tracks == 0 || tracks > 20 || !reader.has(song, 1u + (inlinePointers ? 2u : 1u) * tracks)) {
    return std::nullopt;
  }
  for (u32 i = 0; i < tracks; ++i) {
    const u32 slot = inlinePointers ? song + 1 + 2 * i : reader.le16(tables + 16) + 2u * reader.u8At(song + 1 + i);
    if (!reader.has(slot, 2)) {
      return std::nullopt;
    }
    const u16 start = reader.le16(slot);
    if (start < 0x200 || start == 0xffff) {
      return std::nullopt;
    }
  }
  // The two clocks must agree for the current frame-based scheduler.
  if (reader.u8At(*timers + 1) != reader.u8At(*timers + 4) || reader.u8At(0xfb) != reader.u8At(*timers + 4)) {
    return std::nullopt;
  }
  return Layout{
      .revision = Revision::Late,
      .tables = tables,
      .directory = static_cast<u16>(reader.u8At(*timers + 16) | (reader.u8At(*timers + 19) << 8)),
      .song = song,
      .pitchTable = pitchTable,
      .frameMicroseconds = 125u * (reader.u8At(*timers + 1) == 0 ? 256u : reader.u8At(*timers + 1)),
      .tracks = tracks,
      .resetCommand = reader.u8At(reader.le16(commands + 14)) == 0x3f,
      .inlineTrackPointers = inlinePointers,
      .sharedTranspose = reader.le16(phrase + 0x38) == reader.le16(transpose + 4),
  };
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto lookup = findBytePattern(reader, kLookup);
  const auto songCode = findBytePattern(reader, kSong);
  const auto phrase = findBytePattern(reader, kPhrase);
  const auto earlyList = findBytePattern(reader, kEarlyList);
  const auto dispatch = findBytePattern(reader, kDispatch);
  auto pitch = findBytePattern(reader, kPitch);
  Revision revision = earlyList ? Revision::Early : Revision::Standard;
  if (!pitch) {
    pitch = findBytePattern(reader, kExtendedPitch);
    revision = Revision::Extended;
  }
  if (lookup && songCode && pitch && findBytePattern(reader, kLateDispatch)) {
    return findLateLayout(reader, *lookup, *songCode, *pitch);
  }
  const auto delta = findBytePattern(reader, kDelta);
  const auto directory = findBytePattern(reader, kDirectory);
  const auto timer = findBytePattern(reader, kTimer);
  const auto frame = findBytePattern(reader, kFrame);
  if (!lookup || !songCode || !dispatch || !pitch || !delta || !directory || !timer || !frame ||
      reader.le16(*songCode + 3) != *lookup || reader.u8At(*frame + 8) == 0 ||
      reader.u8At(*frame + 8) != reader.u8At(*frame + 12)) {
    return std::nullopt;
  }
  const u16 commands = reader.le16(*dispatch + 6);
  const u16 tables = reader.le16(*lookup + 8);
  const u16 pitchTable = reader.le16(*pitch + 1);
  const u16 deltaTable = reader.le16(*delta + 1);
  if (revision == Revision::Early) {
    const u16 fetch = reader.le16(*earlyList + 1);
    const u16 lists = reader.le16(*earlyList + 22);
    if (fetch < 7 || !matchesBytePattern(reader, fetch - 7, kEarlyDispatch) ||
        !matchesBytePattern(reader, lists, kEarlyDispatch) || reader.le16(*earlyList + 6) != *lookup) {
      return std::nullopt;
    }
    const u16 trackCommands = reader.le16(fetch - 2);
    const u16 listCommands = reader.le16(lists + 5);
    if (!reader.has(trackCommands, 12) || !reader.has(listCommands, 24) || !reader.has(commands, 10) ||
        reader.le16(trackCommands + 8) != *earlyList ||
        !matchesBytePattern(reader, reader.le16(listCommands + 2), kEarlyPattern) ||
        !matchesBytePattern(reader, reader.le16(commands), kEarlyPatternEnd) ||
        reader.le16(reader.le16(commands) + 7) != lists) {
      return std::nullopt;
    }
  } else if (!phrase || !reader.has(commands, 22) || reader.le16(commands + 12) != *phrase ||
             reader.le16(commands + 14) != reader.le16(commands + 16)) {
    return std::nullopt;
  }
  if (revision == Revision::Extended &&
      (*pitch < 57 || !matchesBytePattern(reader, *pitch - 57, kPitchBias) || !reader.has(commands, 26) ||
       !matchesBytePattern(reader, reader.le16(commands + 22), kLongGate) ||
       !matchesBytePattern(reader, reader.le16(commands + 24), kLegato))) {
    return std::nullopt;
  }
  if (!reader.has(tables, 32) || reader.le16(*lookup + 15) != tables + 1 ||
      reader.le16(*pitch + 6) != pitchTable + 240 || reader.le16(*delta + 8) != deltaTable + 32 ||
      !reader.has(pitchTable, 480) || !reader.has(deltaTable, 64)) {
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
  const u16 trackTable = reader.le16(tables + (revision == Revision::Early ? 0x18 : 0x10));
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

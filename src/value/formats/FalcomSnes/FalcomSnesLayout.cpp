/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/FalcomSnes/FalcomSnes.h"

#include "value/scan/BytePattern.h"

namespace vgmtrans::formats::falcom_snes {

using namespace core;

namespace {

// Ys V $0c05: load eight song-relative track pointers through $b9/$ba.
constexpr auto kLoadSequence = makeMaskedBytePattern(
    "\x4b\x67\xf7\xb9\xd4\x73\xc4\x00\xfc\xf7\xb9\xc4\x01\x60\x84\xba"
    "\xd4\x7d\x09\x01\x00\xf0\x03\x18\x80\x67\xfc\x3d\xc8\x08\xd0\xe0",
    "x?x?x?xxxx?xxxx?x?xxxxxxx?xxxxxx");

// Ys V $0b2c: initialize DSP DIR.
constexpr auto kLoadDirectory = makeMaskedBytePattern("\xe8\x1b\x8f\x5d\xf2\xc4\xf3", "x?xxxxx");

// Ys V $1446: map the encoded instrument to SRCN, then index a five-byte row.
constexpr auto kLoadInstrument = makeMaskedBytePattern(
    "\xcd\x00\x75\x7c\x0b\xf0\x03\x3d\x2f\xf8\x8d\x05\xcf\xfd\x7d\xf8"
    "\xaa\xd5\x28\x02\xf6\x95\x11",
    "xxx??xxxxxxxxxxx?x??x??");

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto sequenceCode = findBytePattern(reader, kLoadSequence);
  const auto directoryCode = findBytePattern(reader, kLoadDirectory);
  const auto instrumentCode = findBytePattern(reader, kLoadInstrument);
  if (!sequenceCode || !directoryCode || !instrumentCode) {
    return std::nullopt;
  }

  const u8 sequencePointer = reader.u8At(*sequenceCode + 3);
  if (reader.u8At(*sequenceCode + 15) != static_cast<u8>(sequencePointer + 1) ||
      !reader.has(sequencePointer, 2)) {
    return std::nullopt;
  }
  const u16 header = reader.le16(sequencePointer);
  const u16 directory = static_cast<u16>(reader.u8At(*directoryCode + 1) << 8);
  const u16 srcnMap = reader.le16(*instrumentCode + 3);
  const u16 instruments = reader.le16(*instrumentCode + 21);
  if (!reader.has(header, 0x20) || !reader.has(directory, 4) || !reader.has(srcnMap, 1) ||
      !reader.has(instruments, 5)) {
    return std::nullopt;
  }

  std::array<std::optional<u16>, kTrackCount> tracks;
  bool hasTrack = false;
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u16 relative = reader.le16(header + track * 2);
    if (relative == 0) {
      continue;
    }
    const u16 start = static_cast<u16>(header + relative);
    if (!reader.has(start, 1)) {
      return std::nullopt;
    }
    tracks[track] = start;
    hasTrack = true;
  }
  if (!hasTrack) {
    return std::nullopt;
  }
  return Layout{
      .sequenceHeaderAddress = header,
      .instrumentTableAddress = instruments,
      .instrumentSrcnMapAddress = srcnMap,
      .spcDirAddress = directory,
      .trackStarts = tracks,
  };
}

}  // namespace vgmtrans::formats::falcom_snes

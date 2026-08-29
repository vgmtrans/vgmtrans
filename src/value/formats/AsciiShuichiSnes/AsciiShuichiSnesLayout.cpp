/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AsciiShuichiSnes/AsciiShuichiSnes.h"

#include "value/scan/BytePattern.h"

namespace vgmtrans::formats::ascii_shuichi_snes {

using namespace core;

namespace {

constexpr auto kLoadSequence = makeMaskedBytePattern(
    "\xe8\x05\x3f\x00\x00\xf5\x00\x00\xd5\x00\x00\xf5\x00\x00\xd4\x00\x1d\x10\xed",
    "xxx??x??x??x??x?xxx");
constexpr auto kLoadLaterInstrument = makeMaskedBytePattern(
    "\xf8\x00\x1c\x1c\xc4\x00\xfd\xf4\x00\xc4\x00\xf7\x00\xeb\x00\xdc\x3f\x00\x00\xfd\xf7\x00\xd5\x00\x00",
    "x?xxx?xx?x?x?x?xx??xx?x??");
constexpr auto kLoadEarlyInstrument = makeMaskedBytePattern(
    "\xf8\x00\x1c\x1c\xc4\x00\xfd\xf7\x00\xfd\xf5\x00\x00\x3f\x00\x00\xf7\x00\xd5\x00\x00",
    "x?xxx?xx?xx??x??x?x??");
constexpr auto kLoadDir = makeMaskedBytePattern("\x8d\x5d\xe8\x00\x3f\x00\x00", "xxx?x??");
constexpr auto kDispatch = makeMaskedBytePattern(
    "\x80\xa8\x00\x10\x0b\x60\x88\x00\x1c\x5d\xaa\x00\x00\x1f\x00\x00", "xx?xxxx?xxx??x??");

[[nodiscard]] std::optional<std::array<u16, kTrackCount>> readTracks(ByteReader reader, u16 header) {
  if (!reader.has(header, kTrackCount * 2)) {
    return std::nullopt;
  }
  std::array<u16, kTrackCount> tracks{};
  for (u32 index = 0; index < kTrackCount; ++index) {
    tracks[index] = static_cast<u16>(reader.u8At(header + index) | (reader.u8At(header + kTrackCount + index) << 8));
    if (tracks[index] == 0 || !reader.has(tracks[index], 1)) {
      return std::nullopt;
    }
  }
  return tracks;
}

}  // namespace

const char* versionName(Version version) {
  return version == Version::Early ? "early" : "later";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto sequenceLoader = findBytePattern(reader, kLoadSequence);
  const auto dirLoader = findBytePattern(reader, kLoadDir);
  const auto dispatch = findBytePattern(reader, kDispatch);
  if (!sequenceLoader || !dirLoader || !dispatch) {
    return std::nullopt;
  }

  const u16 header = reader.le16(*sequenceLoader + 6);
  const auto tracks = readTracks(reader, header);
  const u8 noteBase = reader.u8At(*dispatch + 2);
  const u8 commandCount = reader.u8At(*dispatch + 7);
  const Version version = noteBase == 0xa0 && commandCount == 0x20 ? Version::Early : Version::Later;
  if (!tracks || (version == Version::Later && (noteBase != 0xac || commandCount != 0x2c))) {
    return std::nullopt;
  }

  const auto instrumentLoader = findBytePattern(reader, version == Version::Early ? kLoadEarlyInstrument
                                                                                   : kLoadLaterInstrument);
  if (!instrumentLoader) {
    return std::nullopt;
  }
  const u8 instrumentPointer = reader.u8At(*instrumentLoader + (version == Version::Early ? 8 : 12));
  const u8 tuningPointer = reader.u8At(*instrumentLoader + (version == Version::Early ? 17 : 21));
  if (!reader.has(instrumentPointer, 2) || !reader.has(tuningPointer, 2)) {
    return std::nullopt;
  }

  const u16 instrumentTable = reader.le16(instrumentPointer);
  const u16 tuningTable = reader.le16(tuningPointer);
  const u16 spcDir = static_cast<u16>(reader.u8At(*dirLoader + 3) << 8);
  const u16 commandTable = reader.le16(*dispatch + 14);
  if (!reader.has(instrumentTable, 4) || !reader.has(tuningTable, 1) || !reader.has(spcDir, 4) ||
      !reader.has(commandTable, commandCount * 2u)) {
    return std::nullopt;
  }

  const u32 firIndex = version == Version::Early ? 0x1e : 0x22;
  return Layout{
      .version = version,
      .noteBase = noteBase,
      .sequenceHeaderAddress = header,
      .instrumentTableAddress = instrumentTable,
      .tuningTableAddress = tuningTable,
      .spcDirAddress = spcDir,
      .commandTableAddress = commandTable,
      .hasEchoFirCommand = reader.le16(commandTable + firIndex * 2u) != 0,
      .trackAddresses = *tracks,
  };
}

}  // namespace vgmtrans::formats::ascii_shuichi_snes

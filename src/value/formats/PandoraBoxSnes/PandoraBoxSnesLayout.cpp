/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PandoraBoxSnes/PandoraBoxSnes.h"

#include "value/scan/BytePattern.h"

namespace vgmtrans::formats::pandora_box_snes {

using namespace core;

namespace {

// V1 reads each relative track pointer through one direct-page song pointer.
constexpr auto kLoadStandardTrack = makeMaskedBytePattern(
    "\x8d\x10\xfc\xf7\x00\xdc\x37\x00\x68\xff\xf0\x00", "xxxx?xx?xxx?");

// Traverse multiplexes four players. Player zero is the persistent music slot;
// $08 is only the work pointer for whichever player the scheduler is visiting.
constexpr auto kLoadTraverseTrack = makeMaskedBytePattern(
    "\x8d\x10\x7d\xf0\x00\x6d\xf7\x08\xfc\xc4\x00\xf7\x08\xfc\xc4\x01\xbc\xf0\x00",
    "xxxx?xxxxx?x?xx?xx?");

// The immediate is the DSP DIR page. It differs between the two driver lines.
constexpr auto kSetDir = makeMaskedBytePattern("\xe8\x00\x8d\x5d\x61", "x?xxx");

// Instrument selection adds byte $0c of the song header, loads a global ID,
// and searches the driver's global table backward. The two absolute operands
// reveal the table count and its address without relying on game-specific RAM.
constexpr auto kLoadSrcn = makeMaskedBytePattern(
    "\x8d\x0c\x60\x97\x00\xfd\xf7\x00\xec\x00\x00\xf0\x07\x76\x00\x00", "xxxx?xx?x??xxx??");

[[nodiscard]] std::optional<u16> standardHeader(ByteReader reader, u32 loader) {
  const u8 pointer = reader.u8At(loader + 4);
  if (reader.u8At(loader + 7) != pointer || !reader.has(pointer, 2)) {
    return std::nullopt;
  }
  return reader.le16(pointer);
}

[[nodiscard]] bool plausibleHeader(ByteReader reader, u16 address) {
  if (address < 0x0200 || !reader.has(address, kSequenceHeaderSize)) {
    return false;
  }
  const u8 tempo = reader.u8At(address + 6);
  const u8 timebase = reader.u8At(address + 7);
  if (tempo == 0 || timebase == 0 || (timebase & 3) != 0) {
    return false;
  }
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u16 relative = reader.le16(address + 0x10 + track * 2);
    if (relative != 0xffff && reader.has(static_cast<u16>(address + relative), 1)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<u16> findStandardHeader(ByteReader reader, u32 firstLoader) {
  for (std::optional<u32> loader = firstLoader; loader;
       loader = findBytePattern(reader, kLoadStandardTrack, *loader + 1)) {
    if (const auto header = standardHeader(reader, *loader); header && plausibleHeader(reader, *header)) {
      return header;
    }
  }
  return std::nullopt;
}

[[nodiscard]] EchoState readEcho(ByteReader reader, u16 header) {
  EchoState echo;
  if (reader.u8At(header + 0x20) != 0xff) {
    echo.masterVolume = reader.s8At(header + 0x20);
    echo.volume = reader.s8At(header + 0x21);
    echo.delay = reader.u8At(header + 0x22) & 0x0f;
    echo.feedback = reader.s8At(header + 0x23);
    for (u32 tap = 0; tap < echo.fir.size(); ++tap) {
      echo.fir[tap] = reader.s8At(header + 0x24 + tap);
    }
  }
  return echo;
}

}  // namespace

const char* versionName(Version version) {
  return version == Version::Traverse ? "Traverse" : "Standard";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }

  Version version;
  std::optional<u16> header;
  constexpr u32 codeBegin = 0xe000;
  if (findBytePattern(reader, kLoadTraverseTrack, codeBegin)) {
    version = Version::Traverse;
    // $01c6 is the base of Traverse's persistent four-player header table.
    if (reader.has(0x01c6, 2) && plausibleHeader(reader, reader.le16(0x01c6))) {
      header = reader.le16(0x01c6);
    } else if (reader.has(8, 2) && plausibleHeader(reader, reader.le16(8))) {
      header = reader.le16(8);
    }
  } else if (const auto loader = findBytePattern(reader, kLoadStandardTrack, codeBegin)) {
    version = Version::Standard;
    header = findStandardHeader(reader, *loader);
  } else {
    return std::nullopt;
  }
  const auto dirCode = findBytePattern(reader, kSetDir, codeBegin);
  const auto srcnCode = findBytePattern(reader, kLoadSrcn, codeBegin);
  if (!header || !dirCode || !srcnCode) {
    return std::nullopt;
  }

  const u16 countAddress = reader.le16(*srcnCode + 9);
  const u16 compareBase = reader.le16(*srcnCode + 14);
  const u16 globalTable = static_cast<u16>(compareBase + 1);
  const u8 globalCount = reader.has(countAddress, 1) ? reader.u8At(countAddress) : 0;
  const u16 directory = static_cast<u16>(reader.u8At(*dirCode + 1) << 8);
  const u8 localTableOffset = reader.u8At(*header + 0x0c);
  const u16 localTable = static_cast<u16>(*header + localTableOffset);
  if (globalCount == 0 || globalCount > 0x40 || !reader.has(globalTable, globalCount) ||
      !reader.has(localTable, 1) || !reader.has(directory, 4)) {
    return std::nullopt;
  }

  Layout layout{
      .version = version,
      .sequenceHeaderAddress = *header,
      .localInstrumentTableOffset = localTableOffset,
      .globalInstrumentTableAddress = globalTable,
      .globalInstrumentCount = globalCount,
      .spcDirAddress = directory,
      .initialTempo = reader.u8At(*header + 6),
      .timebase = reader.u8At(*header + 7),
      .echo = readEcho(reader, *header),
  };
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u32 pointer = *header + 0x10 + track * 2;
    const u16 relative = reader.le16(pointer);
    if (relative == 0xffff) {
      continue;
    }
    const u16 address = static_cast<u16>(*header + relative);
    if (reader.has(address, 1)) {
      layout.tracks[track] = address;
    }
  }
  return layout;
}

u8 programSrcn(ByteReader reader, const Layout& layout, u8 program) {
  const u16 local = localInstrumentAddress(layout, program);
  const u8 global = reader.u8At(local);
  for (u32 index = layout.globalInstrumentCount; index != 0; --index) {
    if (reader.u8At(layout.globalInstrumentTableAddress + index - 1) == global) {
      return static_cast<u8>(index - 1);
    }
  }
  // The failure paths are different in the two binaries: V1 leaves Y at zero,
  // while Traverse loads $40 and then decrements it before writing SRCN.
  return layout.version == Version::Traverse ? u8{0x3f} : u8{0};
}

}  // namespace vgmtrans::formats::pandora_box_snes

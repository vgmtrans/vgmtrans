/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WinkysoftSnes/WinkysoftSnes.h"
#include "value/scan/BytePattern.h"

namespace vgmtrans::formats::winkysoft_snes {

using namespace core;

namespace {

constexpr auto kDispatch = makeMaskedBytePattern(
    "\xf7\x08\x30\x00\xc4\x00\x5d\x3a\x08\xf5\x00\x00\x5d\xf7\x08\x1f\x00\x00",
    "xxx?xxxxxx??xxxx??");
constexpr auto kAddresses = makeMaskedBytePattern(
    "\x8d\x00\xe8\x00\xda\x0a\x8d\x00\xe8\x00\xda\x0c", "x?x?xxx?x?xx");
constexpr auto kPitch = makeMaskedBytePattern(
    "\xf6\x00\x00\xc4\x01\xf6\x00\x00\xc4\x00\xf6\x00\x00\x2d\xf6\x00\x00\xee\x9a\x00",
    "x??xxx??xxx??xx??xxx");
constexpr auto kDsp = makeMaskedBytePattern(
    "\xf6\x00\x00\xc4\xf2\xf6\x00\x00\xc4\xf3\xfc\xad\x0f", "x??xxx??xxxxx");
constexpr auto kEarlyDsp = makeMaskedBytePattern(
    "\xf6\x00\x00\xc5\xf2\x00\xf6\x00\x00\xc5\xf3\x00\xfc\xad\x0f", "x??xxxx??xxxxxx");
constexpr auto kPan = makeMaskedBytePattern(
    "\xf5\x00\x00\xcf\xcb\xf3\xeb\x00\xf5\x00\x00\xcf", "x??xxxxxx??x");
constexpr auto kSong = makeMaskedBytePattern(
    "\x8d\x00\xe8\x00\xda\x00\x8f\xff\x08\x8f\xff\x09\x0b\x18\xf8\x18\xf5\x00\x00",
    "x?x?xxxxxxxxxxxxx??");
constexpr auto kTimer = makeMaskedBytePattern(
    "\x8f\x00\xf1\xe4\x00\xc4\xfa\x8f\x01\xf1", "xxxx?xxxxx");

[[nodiscard]] u16 immediateAddress(ByteReader reader, u32 code) {
  return static_cast<u16>((reader.u8At(code + 1) << 8) | reader.u8At(code + 3));
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto dispatch = findBytePattern(reader, kDispatch);
  const auto addresses = findBytePattern(reader, kAddresses);
  const auto pitch = findBytePattern(reader, kPitch);
  auto dsp = findBytePattern(reader, kDsp);
  const bool early = !dsp;
  if (early) {
    dsp = findBytePattern(reader, kEarlyDsp);
  }
  if (!dispatch || !addresses || !pitch || !dsp) {
    return std::nullopt;
  }

  Layout layout;
  layout.commandMap = reader.le16(*dispatch + 10);
  const u16 commandTable = reader.le16(*dispatch + 16);
  layout.instrumentTable = immediateAddress(reader, *addresses);
  layout.pitchTable = reader.le16(*pitch + 6);
  if (!reader.has(layout.commandMap, 128) || !reader.has(commandTable, 60) ||
      !reader.has(layout.instrumentTable, 8) || !reader.has(layout.pitchTable, 26)) {
    return std::nullopt;
  }
  // A shared two-stage dispatch map identifies the family independently of
  // driver relocation. The final six commands have never changed slots.
  constexpr std::array<u8, 6> tail{0x0e, 0x0c, 0x0a, 0x08, 0x06, 0x04};
  if (!matchesBytes(reader, layout.commandMap + 0x7a, tail)) {
    return std::nullopt;
  }
  for (u32 key = 0; key < 0x67; ++key) {
    if (reader.u8At(layout.commandMap + key) != 2) {
      return std::nullopt;
    }
  }
  const auto handler = [&](u8 opcode) {
    return reader.le16(commandTable + reader.u8At(layout.commandMap + opcode));
  };
  for (u32 opcode = 0x67; opcode < 0x80; ++opcode) {
    const u8 slot = reader.u8At(layout.commandMap + opcode);
    if ((slot & 1) != 0 || slot > 0x3a) {
      return std::nullopt;
    }
  }
  layout.version = early ? (handler(0x76) == 0 ? Version::Early : Version::Jojo)
                         : (reader.u8At(layout.commandMap + 0x67) == 2 ? Version::Standard : Version::Extended);

  const u16 dspRegisters = reader.le16(*dsp + 1);
  const u16 dspValues = reader.le16(*dsp + (early ? 7 : 6));
  if (!reader.has(dspRegisters, 15) || !reader.has(dspValues, 15)) {
    return std::nullopt;
  }
  for (u32 i = 0; i < 15; ++i) {
    layout.initialDsp[reader.u8At(dspRegisters + i) & 0x7f] = reader.u8At(dspValues + i);
  }
  layout.directory = static_cast<u16>(layout.initialDsp[0x5d] << 8);
  layout.hostCondition = reader.u8At(0x16);
  if (early) {
    constexpr auto stereo = makeMaskedBytePattern("\xe8\x40\x73\x00\x03\xf5", "xxx?xx");
    if (const auto code = findBytePattern(reader, stereo)) {
      layout.stereo = (reader.u8At(reader.u8At(*code + 3)) & 8) != 0;
    }
  } else {
    layout.stereo = (reader.u8At(0x16) & 0x80) != 0;
    const auto pan = findBytePattern(reader, kPan);
    if (!pan) {
      return std::nullopt;
    }
    layout.panTableLeft = reader.le16(*pan + 1);
    layout.panTableRight = reader.le16(*pan + 9);
    if (!reader.has(layout.panTableLeft, 16) || !reader.has(layout.panTableRight, 16)) {
      return std::nullopt;
    }
  }
  if (const auto timer = findBytePattern(reader, kTimer)) {
    const u8 value = reader.u8At(reader.u8At(*timer + 4));
    layout.frameMicroseconds = (value == 0 ? 256u : value) * 125;
  }
  if (layout.version == Version::Extended) {
    constexpr auto panSequence = makeMaskedBytePattern(
        "\x1c\x5d\xf5\x00\x00\xc4\x00\xf5\x00\x00\xf8", "xxx??xxx??x");
    const auto code = findBytePattern(reader, panSequence, handler(0x68), handler(0x68) + 100);
    if (!code) {
      return std::nullopt;
    }
    layout.panSequenceTable = reader.le16(*code + 3);
    if (!reader.has(layout.panSequenceTable, 256)) {
      return std::nullopt;
    }
    // Masoukishin removed the duration/tempo reset in the infinite-loop arm.
    const std::array<u8, 6> reset{0xe8, 0, 0xc4, 0x0f, 0xc4, 0x0e};
    const auto found = findBytes(reader, reset, handler(0x75));
    layout.resetTempoAtLoop = found && *found < handler(0x75) + 64;
  }

  if (const auto song = findBytePattern(reader, kSong)) {
    if (!reader.has(*song, 32)) { return std::nullopt; }
    layout.songStart = immediateAddress(reader, *song);
    const u16 tempos = reader.le16(*song + 17);
    const u8 tempoVariable = reader.u8At(*song + 22);
    const u8 liveTempo = reader.u8At(tempoVariable);
    const u8 liveVolume = reader.u8At(reader.u8At(*song + 31));
    const auto matches = [&](u32 index) {
      return reader.has(tempos + index, 2) && reader.u8At(tempos + index) != 0 &&
             (liveTempo == 0 || (reader.u8At(tempos + index) == liveTempo &&
                                reader.u8At(tempos + index + 1) == liveVolume));
    };
    u32 index = reader.u8At(0x18) & 0xfe;
    if (!matches(index)) {
      index = 0;
      while (index < 256 && !matches(index)) {
        index += 2;
      }
    }
    if (!reader.has(tempos + index, 2) || index >= 256) {
      return std::nullopt;
    }
    layout.songHeader = static_cast<u16>(tempos + index);
    layout.initialTempo = reader.u8At(layout.songHeader);
    layout.initialVolume = reader.u8At(layout.songHeader + 1);
  } else {
    // Older builds store six-byte descriptors: priority, tempo, volume,
    // logical channel, and absolute stream address. Music has priority zero.
    const u16 songs = immediateAddress(reader, *addresses + 6);
    const u8 liveTempo = reader.u8At(early ? 0x18 : 0x22);
    const u32 selected = early ? static_cast<u8>(reader.u8At(0x15) - 8) : reader.u8At(0x18);
    const auto valid = [&](u32 address) {
      return reader.has(address, 6) && reader.u8At(address) == 0 && reader.u8At(address + 1) != 0 &&
             (liveTempo == 0 || reader.u8At(address + 1) == liveTempo) &&
             reader.u8At(address + 3) < kTrackCount && reader.le16(address + 4) >= 0x200;
    };
    u32 header = songs + selected * 6;
    if (!valid(header)) {
      header = songs;
      for (u32 i = 1; i < 256 && !valid(header); ++i) {
        header += 6;
      }
    }
    if (!valid(header)) {
      return std::nullopt;
    }
    layout.songHeader = static_cast<u16>(header);
    layout.songHeaderSize = 6;
    layout.songStart = reader.le16(header + 4);
    layout.firstTrack = reader.u8At(header + 3);
    layout.initialTempo = reader.u8At(header + 1);
    layout.initialVolume = reader.u8At(header + 2);
  }
  if (layout.initialTempo == 0 || layout.songStart < 0x200 || !reader.has(layout.songStart, 1)) {
    return std::nullopt;
  }
  return layout;
}

DriverData readDriverData(ByteReader reader, const Layout& layout) {
  DriverData data;
  for (u32 i = 0; i < data.patches.size(); ++i) {
    const u32 address = layout.instrumentTable + i * 8;
    if (!reader.has(address, 8)) {
      break;
    }
    data.patches[i] = Patch{
        .range = reader.range(address, 8), .sample = reader.u8At(address),
        .adsr1 = reader.u8At(address + 1), .adsr2 = reader.u8At(address + 2),
        .pitch = reader.be16(address + 3), .volume = reader.u8At(address + 5),
        .pan = reader.u8At(address + 6), .transpose = reader.u8At(address + 7),
    };
  }
  for (u32 i = 0; i < data.pitches.size(); ++i) {
    data.pitches[i] = reader.le16(layout.pitchTable + i * 2);
  }
  if (layout.panTableLeft != 0) {
    for (u32 i = 0; i < 16; ++i) {
      data.panLeft[i] = reader.u8At(layout.panTableLeft + i);
      data.panRight[i] = reader.u8At(layout.panTableRight + i);
    }
  }
  return data;
}

}  // namespace vgmtrans::formats::winkysoft_snes

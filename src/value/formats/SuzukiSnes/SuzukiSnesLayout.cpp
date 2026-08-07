/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiSnes/SuzukiSnes.h"

#include "value/scan/BytePattern.h"

#include <string_view>

namespace vgmtrans::formats::suzuki_snes {

using namespace core;
using namespace std::string_view_literals;

namespace {

constexpr MaskedBytePattern kLoadSongSd3{
    "\xfa\xf5\x5c\xfa\x5c\xf5\x3f\x0f\x0a\xcd\x00\xe4\x1a\x1c\xfd\xf5\x00\x20\xd6\x79\x1b\xf5\x01\x20\xd6\x7a\x1b\x3d\x3d"sv,
    "x??x??x??xxx?xxx??x??x??x??xx"sv,
};

constexpr MaskedBytePattern kLoadSongBl{
    "\xfa\xf5\x5f\x3f\xfe\x09\x3f\x8a\x04\x8f\x08\x06\xe4\x1d\x1c\x5d\xf6\x00\x20\xd5\x4c\x1b\xf6\x01\x20\xd5\x4d\x1b\x3d\x3d"sv,
    "x??x??x??x??x?xxx??x??x??x??xx"sv,
};

constexpr MaskedBytePattern kDispatchBl{
    "\x80\xa8\xc4\x2d\x5d\xf5\x21\x17\x28\x07\xc4\x06\x8d\x00\xcd\x00\x8b\x06\xf0\x09\xf7\x29\xd4\x0e\x3a\x29\x3d\x2f\xf3\xae\x1c\x5d\x60\xeb\x1e\x1f\xa9\x16"sv,
    "xxxxxx??xxx?xxxxx?x?x?x?x?xx?xxxxx?x??"sv,
};

constexpr MaskedBytePattern kLoadDir{
    "\x8f\x5d\xf2\x8f\x5e\xf3"sv,
    "xxxx?x"sv,
};

constexpr MaskedBytePattern kLoadInstrument{
    "\xd6\x48\x01\x5d\xf5\x80\x5e\x1c\x5d\xf5\x01\x5f\xd6\x60\x01\xeb\x23\xf5\x40\x5f\xd6\x78\x01\xf5\x41\x5f\xd6\x79\x01\xf5\x80\x5f\xd6\xa8\x01\xf5\x81\x5f\xd6\xa9\x01"sv,
    "x??xx??x" "xx??x??x" "?x??x??x" "??x??x??" "x??x??x?" "?"sv,
};

[[nodiscard]] bool validHeader(ByteReader reader, Version version, u32 address) {
  if (!reader.has(address, version == Version::SeikenDensetsu3 ? 17 : 1)) {
    return false;
  }

  u32 drum = version == Version::SeikenDensetsu3 ? address + kTrackCount * 2 : address;
  // A note byte >= $80 terminates the table. At most 128 distinct note slots
  // are meaningful, and the complete pointer table must follow or precede it.
  u32 rows = 0;
  while (reader.has(drum, 1) && reader.u8At(drum) < 0x80 && rows < 128) {
    if (!reader.has(drum, 5)) {
      return false;
    }
    drum += 5;
    ++rows;
  }
  if (!reader.has(drum, 1) || reader.u8At(drum) < 0x80) {
    return false;
  }

  const u32 pointers = version == Version::SeikenDensetsu3 ? address : drum + 1;
  if (!reader.has(pointers, kTrackCount * 2)) {
    return false;
  }
  u32 tracks = 0;
  for (u32 index = 0; index < kTrackCount; ++index) {
    const u16 start = reader.le16(pointers + index * 2);
    if (start != 0 && !reader.has(start, 1)) {
      return false;
    }
    tracks += start != 0;
  }
  return tracks != 0;
}

}  // namespace

const char* versionName(Version version) {
  switch (version) {
    case Version::SeikenDensetsu3:
      return "Seiken Densetsu 3";
    case Version::BahamutLagoon:
      return "Bahamut Lagoon";
    case Version::SuperMarioRpg:
      return "Super Mario RPG";
  }
  return "Unknown";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }

  Version version;
  u16 header = 0;
  if (const auto song = findBytePattern(reader, kLoadSongSd3)) {
    version = Version::SeikenDensetsu3;
    header = reader.le16(*song + 16);
  } else if (const auto laterSong = findBytePattern(reader, kLoadSongBl)) {
    version = Version::BahamutLagoon;
    header = reader.le16(*laterSong + 17);
    if (const auto dispatch = findBytePattern(reader, kDispatchBl)) {
      const u16 lengths = reader.le16(*dispatch + 6);
      // FC consumes three operands only in Super Mario RPG. The table stores
      // total command sizes, hence the discriminating value of four.
      if (reader.has(lengths + 56, 1) && reader.u8At(lengths + 56) == 4) {
        version = Version::SuperMarioRpg;
      }
    }
  } else {
    return std::nullopt;
  }
  if (!validHeader(reader, version, header)) {
    return std::nullopt;
  }

  const auto dir = findBytePattern(reader, kLoadDir);
  const auto instrument = findBytePattern(reader, kLoadInstrument);
  if (!dir || !instrument) {
    return std::nullopt;
  }

  const Layout layout{
      .version = version,
      .sequenceHeaderAddress = header,
      .spcDirAddress = static_cast<u16>(reader.u8At(*dir + 4) << 8),
      .srcnTableAddress = reader.le16(*instrument + 5),
      .volumeTableAddress = reader.le16(*instrument + 10),
      .adsrTableAddress = reader.le16(*instrument + 18),
      .tuningTableAddress = reader.le16(*instrument + 30),
  };
  if (!reader.has(layout.spcDirAddress, 4) || !reader.has(layout.srcnTableAddress, 128) ||
      !reader.has(layout.volumeTableAddress, 1) || !reader.has(layout.adsrTableAddress, 2) ||
      !reader.has(layout.tuningTableAddress, 2)) {
    return std::nullopt;
  }
  return layout;
}

}  // namespace vgmtrans::formats::suzuki_snes

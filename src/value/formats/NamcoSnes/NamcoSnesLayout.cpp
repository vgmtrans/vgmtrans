/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include "value/scan/BytePattern.h"

namespace vgmtrans::formats::namco_snes {

using namespace core;

namespace {

constexpr auto kEnvelopeLoader = makeMaskedBytePattern(
    "\xe5\x00\x00\xc4\x42\xe5\x00\x00\xc4\x43\xf5\x40\x03\x1c\xfd\xf7\x42",
    "x??xxx??xxxxxxxxx");

constexpr auto kTuningLoader =
    makeMaskedBytePattern("\xf5\x00\x02\x1c\x60\x88\x00\xc4\x3c\xe8\x00\x88\x00\xc4\x3d",
                          "xxxxxx?xxx?xxxx");

constexpr auto kDspDefaults = makeMaskedBytePattern(
    "\x0c\x00\x1c\x00\x0d\x00\x2c\x00\x2d\x00\x3c\x00\x3d\x00\x4d\x00\x5d\x00\x6d\x00\x7d\x00\x6c\x00",
    "x?x?x?x?x?x?x?x?x?x?x?x?");

[[nodiscard]] bool plausibleSequence(ByteReader reader, u16 address) {
  if (address == 0 || !reader.has(address, 1)) {
    return false;
  }
  const u8 opcode = reader.u8At(address);
  return opcode <= 0x14 || (opcode >= 0x20 && opcode <= 0x2a);
}

struct SequenceLocation {
  u16 address;
  u16 reference;
  u8 referenceSize;
  u8 songIndex;
};

[[nodiscard]] std::optional<SequenceLocation> inlineSong(ByteReader reader, u16 list, u8 group, u8 state) {
  const u8 song = state & 0x7f;
  const u32 row = list + song * 3u;
  if (!reader.has(row, 3) || reader.u8At(row) != group) {
    return std::nullopt;
  }
  const u16 start = reader.le16(row + 1);
  if (!plausibleSequence(reader, start)) {
    return std::nullopt;
  }
  return SequenceLocation{.address = start,
                          .reference = static_cast<u16>(row),
                          .referenceSize = 3,
                          .songIndex = song};
}

[[nodiscard]] std::optional<SequenceLocation> indirectSong(ByteReader reader, u16 block, u8 group, u8 state) {
  const u8 song = state & 0x7f;
  const u32 listPointer = block + 8 + group * 2u;
  if (!reader.has(listPointer, 2)) {
    return std::nullopt;
  }
  const u16 list = reader.le16(listPointer);
  const u32 row = list + song * 2u;
  if (!reader.has(row, 2)) {
    return std::nullopt;
  }
  const u16 start = reader.le16(row);
  if (!plausibleSequence(reader, start)) {
    return std::nullopt;
  }
  return SequenceLocation{.address = start,
                          .reference = static_cast<u16>(row),
                          .referenceSize = 2,
                          .songIndex = song};
}

[[nodiscard]] std::optional<SequenceLocation> selectSequence(ByteReader reader, Version version, u16 block) {
  const bool indirect = version == Version::YuuYuuHakushoTokubetsuHen;
  const auto read = [&](u8 group, u8 state) {
    return indirect ? indirectSong(reader, block, group, state)
                    : inlineSong(reader, static_cast<u16>(block + 8), group, state);
  };

  // $49/$4b/$4d/$4f retain the song number for the four driver slots. Bit 7
  // is set while a slot is resident, unlike $00-$07, which are live cursors.
  for (const bool requireResident : {true, false}) {
    for (u8 group = 0; group < 4; ++group) {
      const u8 state = reader.u8At(0x49 + group * 2u);
      if ((state & 0x80) != (requireResident ? 0x80 : 0)) {
        continue;
      }
      if (const auto selected = read(group, state)) {
        return selected;
      }
    }
  }

  // Static dumps and early boot snapshots may not retain live slot state.
  if (indirect) {
    for (u8 group = 0; group < 4; ++group) {
      if (const auto selected = indirectSong(reader, block, group, 0)) {
        return selected;
      }
    }
  } else {
    const u16 list = static_cast<u16>(block + 8);
    for (u8 song = 0; song < 0x80; ++song) {
      const u32 row = list + song * 3u;
      if (!reader.has(row, 3) || reader.u8At(row) >= 4) {
        continue;
      }
      const u16 start = reader.le16(row + 1);
      if (plausibleSequence(reader, start)) {
        return SequenceLocation{.address = start,
                                .reference = static_cast<u16>(row),
                                .referenceSize = 3,
                                .songIndex = song};
      }
    }
  }
  return std::nullopt;
}

}  // namespace

const char* versionName(Version version) {
  switch (version) {
    case Version::WagyanParadise:
      return "Wagyan Paradise";
    case Version::YuuYuuHakushoTokubetsuHen:
      return "Yuu Yuu Hakusho: Tokubetsu-hen";
    case Version::BlueCrystalRod:
      return "The Blue Crystal Rod";
  }
  return "Unknown";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }

  const auto envelopeLoader = findBytePattern(reader, kEnvelopeLoader);
  const auto tuningLoader = findBytePattern(reader, kTuningLoader);
  const auto dspDefaults = findBytePattern(reader, kDspDefaults);
  if (!envelopeLoader || !tuningLoader || !dspDefaults) {
    return std::nullopt;
  }

  const u16 block = reader.le16(*envelopeLoader + 1);
  if (reader.le16(*envelopeLoader + 6) != static_cast<u16>(block + 1) || !reader.has(block, 16)) {
    return std::nullopt;
  }
  const Version version = block == 0xc000 ? Version::YuuYuuHakushoTokubetsuHen
                          : block == 0xf000 ? Version::BlueCrystalRod
                                            : Version::WagyanParadise;
  const auto sequence = selectSequence(reader, version, block);
  if (!sequence) {
    return std::nullopt;
  }

  const u16 tuning = static_cast<u16>(reader.u8At(*tuningLoader + 6) | (reader.u8At(*tuningLoader + 10) << 8));
  const u16 directory = static_cast<u16>(reader.u8At(*dspDefaults + 17) << 8);
  if (!reader.has(tuning, 2) || !reader.has(directory, 4)) {
    return std::nullopt;
  }

  return Layout{
      .version = version,
      .sequenceAddress = sequence->address,
      .sequenceReferenceAddress = sequence->reference,
      .sequenceReferenceSize = sequence->referenceSize,
      .songIndex = sequence->songIndex,
      .dataPointerBlockAddress = block,
      .tuningTableAddress = tuning,
      .spcDirAddress = directory,
      .mono = reader.u8At(0xd4) != 0,
  };
}

}  // namespace vgmtrans::formats::namco_snes

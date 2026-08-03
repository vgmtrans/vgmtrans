/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace vgmtrans::formats::akao_snes {

using namespace core;

namespace {

constexpr auto kReadNoteLengthV1 = makeMaskedBytePattern("\xcd\x0f\x8d\x00\x9e\xf8\x27\xf6\xb1\x18", "xxxxxx?x??");
constexpr auto kReadNoteLengthV2 = makeMaskedBytePattern("\x8d\x00\xcd\x0f\x9e\xf8\x06\xf6\xf4\x19", "xxxxxx?x??");
constexpr auto kReadNoteLengthV4 = makeMaskedBytePattern("\xcd\x0e\x9e\xf8\xa2\xf6\xaa\x16", "xxxx?x??");

constexpr auto kVCmdExecFF4 = makeMaskedBytePattern(
    "\xa8\xd2\x1c\xfd\xf6\xee\x17\x2d\xf6\xed\x17\x2d\xdd\x5c\xfd\xf6\x49\x18\xf0\x0a", "x?xxx??xx??xxxxx????");
constexpr auto kVCmdExecRS3 = makeMaskedBytePattern(
    "\xa8\xc4\xc4\xa6\x1c\xfd\xf6\x56\x16\x2d\xf6\x55\x16\x2d\xeb\xa6\xf6\xcd\x16\xd0\x01", "x?x?xxx??xx??xx?x????");

constexpr auto kReadSeqHeaderV1 = makeMaskedBytePattern(
    "\x8d\x01\xcb\x8d\xcd\x00\xf5\x00\x20\xd4\x02\xf5\x01\x20\xd4\x03\xf0\x0a\xdb\x48", "xxx?xxx??x?x??x?x?x?");
constexpr auto kReadSeqHeaderV2 =
    makeMaskedBytePattern("\xcd\x00\x8d\x00\x8f\x01\x93\xf5\x01\x20\xf0\x27\x09\x93\x8e\xd4\x08\xf5\x00\x20\xd4\x07",
                          "xxxxxx?x??x?x??x?x??x?");
constexpr auto kReadSeqHeaderFFMQ =
    makeMaskedBytePattern("\xcd\x10\xf5\xff\x1b\xd4\x0d\x1d\xd0\xf8\xe8\x12\x8d\x1c\x9a\x0e"
                          "\xda\x08\xcd\x0e\x8f\x80\xc1\xe5\x10\x1c\xec\x11\x1c\xda\x36",
                          "xxx??x?xxxx?x?x?x?xxxx?x??x??x?");
constexpr auto kReadSeqHeaderV4 = makeMaskedBytePattern(
    "\xe5\x00\x1c\xc4\x00\xe5\x01\x1c\xc4\x01\xe8\x24\x8d\x1c\x9a\x00\xda\x00", "x??x?x??x?x?x?x?x?");

constexpr auto kLoadDirV1 = makeMaskedBytePattern("\xe8\x1e\x8d\x5d\x3f\xe9\x10", "x?xxx??");
constexpr auto kLoadDirV3 = makeMaskedBytePattern("\x8d\x5d\xe8\x1b\x3f\x55\x06", "xxx?x??");
constexpr auto kLoadInstrV1 = makeMaskedBytePattern("\xd5\xc1\x02\xfd\xf6\x00\xff\xd5\x00\x03\x6f", "x??xxx?x??x");
constexpr auto kLoadVolumeEnvelopeV1 =
    makeMaskedBytePattern("\x1c\xfd\xf6\x00\x1d\xd5\x20\x03\xf6\x01\x1d\xd5\x21\x03", "xxx??x??x??x??");
constexpr auto kLoadInstrV2 = makeMaskedBytePattern("\xd4\xa6\xfd\xf6\x40\x1e\xd5\xa0\x03\xc8\x10\xb0\x06\xe4\x93\x24"
                                                    "\x8f\xd0\x1f\x7d\x9f\x5c\x08\x04\x5d\xd8\xf2\xcb\xf3\x3d\xdd\x1c"
                                                    "\xfd\xf6\x80\x1e\xd8\xf2\xc4\xf3\x3d\xf6\x81\x1e\xd8\xf2\xc4\xf3"
                                                    "\xf8\x06\x6f",
                                                    "x?xx??x?"
                                                    "?xxxxx?x"
                                                    "?xxxxxxx"
                                                    "xxxxxxxx"
                                                    "xx??xxxx"
                                                    "xx??xxxx"
                                                    "x?x");
constexpr auto kLoadInstrV3 = makeMaskedBytePattern("\x1c\xfd\xf6\x00\x1a\xd5\x20\xfb\xf6\x01\x1a\xd5\x21\xfb\xf6\x80"
                                                    "\x1a\xd5\x80\xfc\xf6\x81\x1a\xd5\x81\xfc",
                                                    "xxxx?x??xx?x??xx?x??xx?x??");
constexpr auto kReadPercussionTableV4 =
    makeMaskedBytePattern("\x8d\x03\xcf\xfd\xf6\x22\xf1\x30\x04\x1c\xd5\x81\xf2\xf6\x21\xf1"
                          "\xc4\xa5\xf6\x20\xf1\x3f\xcf\x1a",
                          "xxxxx??xxxx??x??x?x??x??");
constexpr auto kVCmdF9CT = makeMaskedBytePattern("\x28\x0f\xc4\x7b\x6f", "xxx?x");

constexpr std::array<u8, 46> kFF4VcmdLengthTable{0x03, 0x03, 0x01, 0x02, 0x03, 0x03, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01,
                                                 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x02, 0x03,
                                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<u8, 46> kRS1VcmdLengthTable{0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x03, 0x00, 0x03,
                                                 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x02,
                                                 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x03, 0x02, 0x00, 0x01, 0x01, 0x00,
                                                 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<u8, 46> kFF5VcmdLengthTable{0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01,
                                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01,
                                                 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x02, 0x01,
                                                 0x02, 0x02, 0x01, 0x03, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<u8, 46> kSD2VcmdLengthTable{0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01,
                                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01,
                                                 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x02, 0x01,
                                                 0x02, 0x02, 0x01, 0x03, 0x02, 0x02, 0x00, 0x01, 0x00, 0x00};
constexpr std::array<u8, 60> kRS2VcmdLengthTable{
    0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x02, 0x01, 0x02, 0x02, 0x01, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<u8, 60> kLALVcmdLengthTable{
    0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x02, 0x01, 0x02, 0x02, 0x01, 0x03, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<u8, 59> kFF6VcmdLengthTable{0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01,
                                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01,
                                                 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02,
                                                 0x01, 0x03, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00};
constexpr std::array<u8, 60> kFMVcmdLengthTable{0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01,
                                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01,
                                                0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02,
                                                0x01, 0x03, 0x02, 0x02, 0x02, 0x01, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00};
constexpr std::array<u8, 60> kRS3VcmdLengthTable{
    0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x02, 0x01, 0x02, 0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00};
constexpr std::array<u8, 60> kGHVcmdLengthTable{0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01,
                                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01,
                                                0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02,
                                                0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<u8, 60> kBSGameVcmdLengthTable{
    0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x02, 0x01, 0x02, 0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00};

[[nodiscard]] std::optional<AkaoSnesMinorVersion> classifiedMinorVersion(ByteReader reader, u32 lengthTable,
                                                                         u32 addressTable,
                                                                         AkaoSnesMinorVersion current) {
  if (matchesBytes(reader, lengthTable, kFF4VcmdLengthTable)) {
    return AKAOSNES_V1_FF4;
  }
  if (matchesBytes(reader, lengthTable, kRS1VcmdLengthTable)) {
    return AKAOSNES_V2_RS1;
  }
  if (matchesBytes(reader, lengthTable, kFF5VcmdLengthTable)) {
    return current == AKAOSNES_V3_FFMQ ? current : AKAOSNES_V3_FF5;
  }
  if (matchesBytes(reader, lengthTable, kSD2VcmdLengthTable)) {
    return AKAOSNES_V3_SD2;
  }
  if (matchesBytes(reader, lengthTable, kRS2VcmdLengthTable)) {
    return AKAOSNES_V4_RS2;
  }
  if (matchesBytes(reader, lengthTable, kLALVcmdLengthTable)) {
    return AKAOSNES_V4_LAL;
  }
  if (matchesBytes(reader, lengthTable, kFF6VcmdLengthTable)) {
    return AKAOSNES_V4_FF6;
  }
  if (matchesBytes(reader, lengthTable, kFMVcmdLengthTable)) {
    if (reader.has(addressTable + 53 * 2, 2)) {
      const u16 commandF9 = reader.le16(addressTable + 53 * 2);
      if (matchesBytePattern(reader, commandF9, kVCmdF9CT)) {
        return AKAOSNES_V4_CT;
      }
    }
    return AKAOSNES_V4_FM;
  }
  if (matchesBytes(reader, lengthTable, kRS3VcmdLengthTable)) {
    return AKAOSNES_V4_RS3;
  }
  if (matchesBytes(reader, lengthTable, kGHVcmdLengthTable)) {
    return AKAOSNES_V4_GH;
  }
  if (matchesBytes(reader, lengthTable, kBSGameVcmdLengthTable)) {
    return AKAOSNES_V4_BSGAME;
  }
  return std::nullopt;
}

}  // namespace

std::optional<AkaoSnesLayout> findAkaoSnesLayout(ByteReader reader) {
  if (reader.size() != kAkaoSnesAramSize) {
    return std::nullopt;
  }

  AkaoSnesVersion noteLengthVersion = AKAOSNES_NONE;
  if (findBytePattern(reader, kReadNoteLengthV4)) {
    noteLengthVersion = AKAOSNES_V4;
  } else if (findBytePattern(reader, kReadNoteLengthV2)) {
    noteLengthVersion = AKAOSNES_V2;
  } else if (findBytePattern(reader, kReadNoteLengthV1)) {
    noteLengthVersion = AKAOSNES_V1;
  } else {
    return std::nullopt;
  }

  u8 firstVCmd = 0;
  u16 vcmdAddressTable = 0;
  u16 vcmdLengthTable = 0;
  if (const auto rs3ExecOffset = findBytePattern(reader, kVCmdExecRS3)) {
    firstVCmd = reader.u8At(*rs3ExecOffset + 1);
    vcmdAddressTable = reader.le16(*rs3ExecOffset + 11);
    vcmdLengthTable = reader.le16(*rs3ExecOffset + 17);
  } else if (const auto ff4ExecOffset = findBytePattern(reader, kVCmdExecFF4)) {
    firstVCmd = reader.u8At(*ff4ExecOffset + 1);
    vcmdAddressTable = reader.le16(*ff4ExecOffset + 9);
    vcmdLengthTable = reader.le16(*ff4ExecOffset + 16);
  } else {
    return std::nullopt;
  }

  u16 sequenceHeaderAddress = 0;
  u16 apuRelocBase = 0;
  bool relocatable = false;
  AkaoSnesMinorVersion minorVersion = AKAOSNES_NOMINORVERSION;
  if (const auto v4HeaderOffset = findBytePattern(reader, kReadSeqHeaderV4)) {
    sequenceHeaderAddress = reader.le16(*v4HeaderOffset + 1);
    apuRelocBase = static_cast<u16>((reader.u8At(*v4HeaderOffset + 13) << 8) | reader.u8At(*v4HeaderOffset + 11));
    relocatable = true;
  } else if (const auto ffmqHeaderOffset = findBytePattern(reader, kReadSeqHeaderFFMQ)) {
    sequenceHeaderAddress = static_cast<u16>(reader.le16(*ffmqHeaderOffset + 3) + 1);
    apuRelocBase = static_cast<u16>((reader.u8At(*ffmqHeaderOffset + 13) << 8) | reader.u8At(*ffmqHeaderOffset + 11));
    relocatable = true;
    minorVersion = AKAOSNES_V3_FFMQ;
  } else if (const auto v2HeaderOffset = findBytePattern(reader, kReadSeqHeaderV2)) {
    sequenceHeaderAddress = reader.le16(*v2HeaderOffset + 18);
    apuRelocBase = sequenceHeaderAddress;
  } else if (const auto v1HeaderOffset = findBytePattern(reader, kReadSeqHeaderV1)) {
    sequenceHeaderAddress = reader.le16(*v1HeaderOffset + 7);
    apuRelocBase = sequenceHeaderAddress;
  } else {
    return std::nullopt;
  }

  // The note-duration routine distinguishes V1/V2/V4, while V3 reuses V2's
  // routine. The first command number and relocated header shape disambiguate
  // the otherwise identical V2 and V3 drivers.
  AkaoSnesVersion version = AKAOSNES_NONE;
  if (noteLengthVersion == AKAOSNES_V1 && firstVCmd == 0xd2 && !relocatable) {
    version = AKAOSNES_V1;
  } else if (noteLengthVersion == AKAOSNES_V2 && firstVCmd == 0xd2 && !relocatable) {
    version = AKAOSNES_V2;
  } else if (noteLengthVersion == AKAOSNES_V2 && firstVCmd == 0xd2 && relocatable) {
    version = AKAOSNES_V3;
  } else if (noteLengthVersion == AKAOSNES_V4 && firstVCmd == 0xc4 && relocatable) {
    version = AKAOSNES_V4;
  } else {
    return std::nullopt;
  }

  if (const auto detected = classifiedMinorVersion(reader, vcmdLengthTable, vcmdAddressTable, minorVersion)) {
    minorVersion = *detected;
  }

  AkaoSnesLayout layout{
      .version = version,
      .minorVersion = minorVersion,
      .sequenceHeaderAddress = sequenceHeaderAddress,
      .apuRelocBase = apuRelocBase,
  };

  if (const auto v1DirOffset = findBytePattern(reader, kLoadDirV1)) {
    layout.spcDirAddress = static_cast<u32>(reader.u8At(*v1DirOffset + 1) << 8);
  } else if (const auto v3DirOffset = findBytePattern(reader, kLoadDirV3)) {
    layout.spcDirAddress = static_cast<u32>(reader.u8At(*v3DirOffset + 3) << 8);
  }

  if (version == AKAOSNES_V1) {
    if (const auto offset = findBytePattern(reader, kLoadInstrV1)) {
      layout.tuningTableAddress = reader.le16(*offset + 5);
    }
    if (const auto offset = findBytePattern(reader, kLoadVolumeEnvelopeV1)) {
      const u16 lowByteTable = reader.le16(*offset + 3);
      const u16 highByteTable = reader.le16(*offset + 9);
      if (highByteTable == static_cast<u16>(lowByteTable + 1)) {
        layout.volumeEnvelopeTableAddress = lowByteTable;
      }
    }
  } else if (version == AKAOSNES_V2) {
    if (const auto offset = findBytePattern(reader, kLoadInstrV2)) {
      layout.tuningTableAddress = reader.le16(*offset + 4);
      layout.adsrTableAddress = reader.le16(*offset + 34);
    }
  } else if (const auto offset = findBytePattern(reader, kLoadInstrV3)) {
    layout.tuningTableAddress = reader.le16(*offset + 3);
    layout.adsrTableAddress = reader.le16(*offset + 15);
  }

  if (const auto offset = findBytePattern(reader, kReadPercussionTableV4)) {
    layout.percussionTableAddress = reader.le16(*offset + 19);
  }

  return layout;
}

}  // namespace vgmtrans::formats::akao_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnesLayout.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string_view>

namespace vgmtrans::formats::akao_snes {

using namespace core;
using namespace std::string_view_literals;

namespace {

struct BytePatternView {
  std::string_view bytes;
  std::string_view mask;
};

constexpr BytePatternView kReadNoteLengthV1{
    "\xcd\x0f\x8d\x00\x9e\xf8\x27\xf6\xb1\x18"sv,
    "xxxxxx?x??"sv,
};
constexpr BytePatternView kReadNoteLengthV2{
    "\x8d\x00\xcd\x0f\x9e\xf8\x06\xf6\xf4\x19"sv,
    "xxxxxx?x??"sv,
};
constexpr BytePatternView kReadNoteLengthV4{
    "\xcd\x0e\x9e\xf8\xa2\xf6\xaa\x16"sv,
    "xxxx?x??"sv,
};

constexpr BytePatternView kVCmdExecFF4{
    "\xa8\xd2\x1c\xfd\xf6\xee\x17\x2d\xf6\xed\x17\x2d\xdd\x5c\xfd\xf6\x49\x18\xf0\x0a"sv,
    "x?xxx??xx??xxxxx????"sv,
};
constexpr BytePatternView kVCmdExecRS3{
    "\xa8\xc4\xc4\xa6\x1c\xfd\xf6\x56\x16\x2d\xf6\x55\x16\x2d\xeb\xa6\xf6\xcd\x16\xd0\x01"sv,
    "x?x?xxx??xx??xx?x????"sv,
};

constexpr BytePatternView kReadSeqHeaderV1{
    "\x8d\x01\xcb\x8d\xcd\x00\xf5\x00\x20\xd4\x02\xf5\x01\x20\xd4\x03\xf0\x0a\xdb\x48"sv,
    "xxx?xxx??x?x??x?x?x?"sv,
};
constexpr BytePatternView kReadSeqHeaderV2{
    "\xcd\x00\x8d\x00\x8f\x01\x93\xf5\x01\x20\xf0\x27\x09\x93\x8e\xd4\x08\xf5\x00\x20\xd4\x07"sv,
    "xxxxxx?x??x?x??x?x??x?"sv,
};
constexpr BytePatternView kReadSeqHeaderFFMQ{
    "\xcd\x10\xf5\xff\x1b\xd4\x0d\x1d\xd0\xf8\xe8\x12\x8d\x1c\x9a\x0e"
    "\xda\x08\xcd\x0e\x8f\x80\xc1\xe5\x10\x1c\xec\x11\x1c\xda\x36"sv,
    "xxx??x?xxxx?x?x?x?xxxx?x??x??x?"sv,
};
constexpr BytePatternView kReadSeqHeaderV4{
    "\xe5\x00\x1c\xc4\x00\xe5\x01\x1c\xc4\x01\xe8\x24\x8d\x1c\x9a\x00\xda\x00"sv,
    "x??x?x??x?x?x?x?x?"sv,
};

constexpr BytePatternView kLoadDirV1{
    "\xe8\x1e\x8d\x5d\x3f\xe9\x10"sv,
    "x?xxx??"sv,
};
constexpr BytePatternView kLoadDirV3{
    "\x8d\x5d\xe8\x1b\x3f\x55\x06"sv,
    "xxx?x??"sv,
};
constexpr BytePatternView kLoadInstrV1{
    "\xd5\xc1\x02\xfd\xf6\x00\xff\xd5\x00\x03\x6f"sv,
    "x??xxx?x??x"sv,
};
constexpr BytePatternView kLoadInstrV2{
    "\xd4\xa6\xfd\xf6\x40\x1e\xd5\xa0\x03\xc8\x10\xb0\x06\xe4\x93\x24"
    "\x8f\xd0\x1f\x7d\x9f\x5c\x08\x04\x5d\xd8\xf2\xcb\xf3\x3d\xdd\x1c"
    "\xfd\xf6\x80\x1e\xd8\xf2\xc4\xf3\x3d\xf6\x81\x1e\xd8\xf2\xc4\xf3"
    "\xf8\x06\x6f"sv,
    "x?xx??x?"
    "?xxxxx?x"
    "?xxxxxxx"
    "xxxxxxxx"
    "xx??xxxx"
    "xx??xxxx"
    "x?x"sv,
};
constexpr BytePatternView kLoadInstrV3{
    "\x1c\xfd\xf6\x00\x1a\xd5\x20\xfb\xf6\x01\x1a\xd5\x21\xfb\xf6\x80"
    "\x1a\xd5\x80\xfc\xf6\x81\x1a\xd5\x81\xfc"sv,
    "xxxx?x??xx?x??xx?x??xx?x??"sv,
};
constexpr BytePatternView kReadPercussionTableV4{
    "\x8d\x03\xcf\xfd\xf6\x22\xf1\x30\x04\x1c\xd5\x81\xf2\xf6\x21\xf1"
    "\xc4\xa5\xf6\x20\xf1\x3f\xcf\x1a"sv,
    "xxxxx??xxxx??x??x?x??x??"sv,
};
constexpr BytePatternView kVCmdF9CT{
    "\x28\x0f\xc4\x7b\x6f"sv,
    "xxx?x"sv,
};

static_assert(kReadNoteLengthV1.bytes.size() == kReadNoteLengthV1.mask.size());
static_assert(kReadNoteLengthV2.bytes.size() == kReadNoteLengthV2.mask.size());
static_assert(kReadNoteLengthV4.bytes.size() == kReadNoteLengthV4.mask.size());
static_assert(kVCmdExecFF4.bytes.size() == kVCmdExecFF4.mask.size());
static_assert(kVCmdExecRS3.bytes.size() == kVCmdExecRS3.mask.size());
static_assert(kReadSeqHeaderV1.bytes.size() == kReadSeqHeaderV1.mask.size());
static_assert(kReadSeqHeaderV2.bytes.size() == kReadSeqHeaderV2.mask.size());
static_assert(kReadSeqHeaderFFMQ.bytes.size() == kReadSeqHeaderFFMQ.mask.size());
static_assert(kReadSeqHeaderV4.bytes.size() == kReadSeqHeaderV4.mask.size());
static_assert(kLoadDirV1.bytes.size() == kLoadDirV1.mask.size());
static_assert(kLoadDirV3.bytes.size() == kLoadDirV3.mask.size());
static_assert(kLoadInstrV1.bytes.size() == kLoadInstrV1.mask.size());
static_assert(kLoadInstrV2.bytes.size() == kLoadInstrV2.mask.size());
static_assert(kLoadInstrV3.bytes.size() == kLoadInstrV3.mask.size());
static_assert(kReadPercussionTableV4.bytes.size() == kReadPercussionTableV4.mask.size());
static_assert(kVCmdF9CT.bytes.size() == kVCmdF9CT.mask.size());

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

[[nodiscard]] bool matchPattern(ByteReader reader, u64 offset, BytePatternView pattern) {
  if (pattern.bytes.size() != pattern.mask.size() || !reader.has(offset, pattern.bytes.size())) {
    return false;
  }
  for (size_t i = 0; i < pattern.bytes.size(); ++i) {
    if (pattern.mask[i] == 'x' && reader.u8At(offset + i) != static_cast<u8>(pattern.bytes[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> searchPattern(ByteReader reader, BytePatternView pattern) {
  if (pattern.bytes.empty() || pattern.bytes.size() != pattern.mask.size() || pattern.bytes.size() > reader.size()) {
    return std::nullopt;
  }
  for (u64 offset = 0; offset <= reader.size() - pattern.bytes.size(); ++offset) {
    if (matchPattern(reader, offset, pattern)) {
      return static_cast<u32>(offset);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool matchBytes(ByteReader reader, std::span<const u8> bytes, u32 offset) {
  if (!reader.has(offset, bytes.size())) {
    return false;
  }
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (reader.u8At(offset + i) != bytes[i]) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<AkaoSnesMinorVersion> classifiedMinorVersion(ByteReader reader, u32 lengthTable,
                                                                         u32 addressTable,
                                                                         AkaoSnesMinorVersion current) {
  if (matchBytes(reader, kFF4VcmdLengthTable, lengthTable)) {
    return AKAOSNES_V1_FF4;
  }
  if (matchBytes(reader, kRS1VcmdLengthTable, lengthTable)) {
    return AKAOSNES_V2_RS1;
  }
  if (matchBytes(reader, kFF5VcmdLengthTable, lengthTable)) {
    return current == AKAOSNES_V3_FFMQ ? current : AKAOSNES_V3_FF5;
  }
  if (matchBytes(reader, kSD2VcmdLengthTable, lengthTable)) {
    return AKAOSNES_V3_SD2;
  }
  if (matchBytes(reader, kRS2VcmdLengthTable, lengthTable)) {
    return AKAOSNES_V4_RS2;
  }
  if (matchBytes(reader, kLALVcmdLengthTable, lengthTable)) {
    return AKAOSNES_V4_LAL;
  }
  if (matchBytes(reader, kFF6VcmdLengthTable, lengthTable)) {
    return AKAOSNES_V4_FF6;
  }
  if (matchBytes(reader, kFMVcmdLengthTable, lengthTable)) {
    if (reader.has(addressTable + 53 * 2, 2)) {
      const u16 commandF9 = reader.le16(addressTable + 53 * 2);
      if (matchPattern(reader, commandF9, kVCmdF9CT)) {
        return AKAOSNES_V4_CT;
      }
    }
    return AKAOSNES_V4_FM;
  }
  if (matchBytes(reader, kRS3VcmdLengthTable, lengthTable)) {
    return AKAOSNES_V4_RS3;
  }
  if (matchBytes(reader, kGHVcmdLengthTable, lengthTable)) {
    return AKAOSNES_V4_GH;
  }
  if (matchBytes(reader, kBSGameVcmdLengthTable, lengthTable)) {
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
  if (searchPattern(reader, kReadNoteLengthV4)) {
    noteLengthVersion = AKAOSNES_V4;
  } else if (searchPattern(reader, kReadNoteLengthV2)) {
    noteLengthVersion = AKAOSNES_V2;
  } else if (searchPattern(reader, kReadNoteLengthV1)) {
    noteLengthVersion = AKAOSNES_V1;
  } else {
    return std::nullopt;
  }

  u8 firstVCmd = 0;
  u16 vcmdAddressTable = 0;
  u16 vcmdLengthTable = 0;
  if (const auto rs3ExecOffset = searchPattern(reader, kVCmdExecRS3)) {
    firstVCmd = reader.u8At(*rs3ExecOffset + 1);
    vcmdAddressTable = reader.le16(*rs3ExecOffset + 11);
    vcmdLengthTable = reader.le16(*rs3ExecOffset + 17);
  } else if (const auto ff4ExecOffset = searchPattern(reader, kVCmdExecFF4)) {
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
  if (const auto v4HeaderOffset = searchPattern(reader, kReadSeqHeaderV4)) {
    sequenceHeaderAddress = reader.le16(*v4HeaderOffset + 1);
    apuRelocBase = static_cast<u16>((reader.u8At(*v4HeaderOffset + 13) << 8) | reader.u8At(*v4HeaderOffset + 11));
    relocatable = true;
  } else if (const auto ffmqHeaderOffset = searchPattern(reader, kReadSeqHeaderFFMQ)) {
    sequenceHeaderAddress = static_cast<u16>(reader.le16(*ffmqHeaderOffset + 3) + 1);
    apuRelocBase = static_cast<u16>((reader.u8At(*ffmqHeaderOffset + 13) << 8) | reader.u8At(*ffmqHeaderOffset + 11));
    relocatable = true;
    minorVersion = AKAOSNES_V3_FFMQ;
  } else if (const auto v2HeaderOffset = searchPattern(reader, kReadSeqHeaderV2)) {
    sequenceHeaderAddress = reader.le16(*v2HeaderOffset + 18);
    apuRelocBase = sequenceHeaderAddress;
  } else if (const auto v1HeaderOffset = searchPattern(reader, kReadSeqHeaderV1)) {
    sequenceHeaderAddress = reader.le16(*v1HeaderOffset + 7);
    apuRelocBase = sequenceHeaderAddress;
  } else {
    return std::nullopt;
  }

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
      .relocatable = relocatable,
      .vcmdAddressTable = vcmdAddressTable,
      .vcmdLengthTable = vcmdLengthTable,
  };

  if (const auto v1DirOffset = searchPattern(reader, kLoadDirV1)) {
    layout.spcDirAddress = static_cast<u32>(reader.u8At(*v1DirOffset + 1) << 8);
  } else if (const auto v3DirOffset = searchPattern(reader, kLoadDirV3)) {
    layout.spcDirAddress = static_cast<u32>(reader.u8At(*v3DirOffset + 3) << 8);
  }

  if (version == AKAOSNES_V1) {
    if (const auto offset = searchPattern(reader, kLoadInstrV1)) {
      layout.tuningTableAddress = reader.le16(*offset + 5);
      layout.adsrTableAddress = 0;
    }
  } else if (version == AKAOSNES_V2) {
    if (const auto offset = searchPattern(reader, kLoadInstrV2)) {
      layout.tuningTableAddress = reader.le16(*offset + 4);
      layout.adsrTableAddress = reader.le16(*offset + 34);
    }
  } else if (const auto offset = searchPattern(reader, kLoadInstrV3)) {
    layout.tuningTableAddress = reader.le16(*offset + 3);
    layout.adsrTableAddress = reader.le16(*offset + 15);
  }

  if (const auto offset = searchPattern(reader, kReadPercussionTableV4)) {
    layout.percussionTableAddress = reader.le16(*offset + 19);
  }

  return layout;
}

std::string akaoSnesSourceDisplayName(const SourceFile& source) {
  if (source.title && !source.title->empty()) {
    return *source.title;
  }
  if (!source.name.empty()) {
    return std::filesystem::path(source.name).stem().string();
  }
  return "AkaoSnes";
}

}  // namespace vgmtrans::formats::akao_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"

#include <optional>
#include <string_view>

namespace vgmtrans::formats::akao_snes {

enum AkaoSnesVersion : u8 {
  AKAOSNES_NONE = 0,
  AKAOSNES_V1,
  AKAOSNES_V2,
  AKAOSNES_V3,
  AKAOSNES_V4,
};

enum AkaoSnesMinorVersion : u8 {
  AKAOSNES_NOMINORVERSION = 0,
  AKAOSNES_V1_FF4,
  AKAOSNES_V2_RS1,
  AKAOSNES_V3_FF5,
  AKAOSNES_V3_SD2,
  AKAOSNES_V3_FFMQ,
  AKAOSNES_V4_RS2,
  AKAOSNES_V4_LAL,
  AKAOSNES_V4_FF6,
  AKAOSNES_V4_FM,
  AKAOSNES_V4_CT,
  AKAOSNES_V4_RS3,
  AKAOSNES_V4_GH,
  AKAOSNES_V4_BSGAME,
};

inline constexpr u32 kAkaoSnesAramSize = 0x10000;
inline constexpr u32 kAkaoSnesMaxTracks = 8;
inline constexpr u16 kAkaoSnesPpqn = 48;
inline constexpr u8 kAkaoSnesDefaultTempo = 0x20;
inline constexpr u8 kAkaoSnesNoteVelocity = 100;
inline constexpr u8 kAkaoSnesDrumKeyBias = 60;
inline constexpr u32 kAkaoSnesDrumKitBank = 0x7f;
inline constexpr u32 kAkaoSnesDrumKitProgram = 0;

[[nodiscard]] constexpr bool akaoSnesRelocatable(AkaoSnesVersion version) {
  return version == AKAOSNES_V3 || version == AKAOSNES_V4;
}

[[nodiscard]] constexpr u8 akaoSnesNoteDurationTableSize(AkaoSnesVersion version) {
  return version == AKAOSNES_V4 ? 14 : 15;
}

[[nodiscard]] constexpr u8 akaoSnesStatusNoteIndexTie(AkaoSnesVersion version) {
  return (version == AKAOSNES_V1 || version == AKAOSNES_V2) ? 13 : 12;
}

[[nodiscard]] constexpr u8 akaoSnesStatusNoteIndexRest(AkaoSnesVersion version) {
  return (version == AKAOSNES_V1 || version == AKAOSNES_V2) ? 12 : 13;
}

[[nodiscard]] constexpr u8 akaoSnesStatusNoteMax(AkaoSnesVersion version) {
  return version == AKAOSNES_V4 ? 0xc3 : 0xd1;
}

[[nodiscard]] constexpr u8 akaoSnesTimer0Frequency(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion) {
  if (version == AKAOSNES_V4) {
    if (minorVersion == AKAOSNES_V4_RS2 || minorVersion == AKAOSNES_V4_LAL) {
      return 0x24;
    }
    if (minorVersion == AKAOSNES_V4_FM || minorVersion == AKAOSNES_V4_CT) {
      return 0x2a;
    }
    return 0x27;
  }
  return 0x24;
}

[[nodiscard]] constexpr bool akaoSnesUses8BitPan(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion) {
  if (version == AKAOSNES_V1 || version == AKAOSNES_V3) {
    return true;
  }
  if (version == AKAOSNES_V2) {
    return false;
  }
  return minorVersion == AKAOSNES_V4_RS2 || minorVersion == AKAOSNES_V4_LAL;
}

[[nodiscard]] constexpr std::string_view akaoSnesVersionName(AkaoSnesVersion version) {
  switch (version) {
    case AKAOSNES_V1:
      return "V1";
    case AKAOSNES_V2:
      return "V2";
    case AKAOSNES_V3:
      return "V3";
    case AKAOSNES_V4:
      return "V4";
    case AKAOSNES_NONE:
    default:
      return "Unknown";
  }
}

[[nodiscard]] constexpr std::string_view akaoSnesMinorVersionName(AkaoSnesMinorVersion version) {
  switch (version) {
    case AKAOSNES_V1_FF4:
      return "FF4";
    case AKAOSNES_V2_RS1:
      return "RS1";
    case AKAOSNES_V3_FF5:
      return "FF5";
    case AKAOSNES_V3_SD2:
      return "SD2";
    case AKAOSNES_V3_FFMQ:
      return "FFMQ";
    case AKAOSNES_V4_RS2:
      return "RS2";
    case AKAOSNES_V4_LAL:
      return "LAL";
    case AKAOSNES_V4_FF6:
      return "FF6";
    case AKAOSNES_V4_FM:
      return "FM";
    case AKAOSNES_V4_CT:
      return "CT";
    case AKAOSNES_V4_RS3:
      return "RS3";
    case AKAOSNES_V4_GH:
      return "GH";
    case AKAOSNES_V4_BSGAME:
      return "BSGAME";
    case AKAOSNES_NOMINORVERSION:
    default:
      return "Unknown";
  }
}

}  // namespace vgmtrans::formats::akao_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatDefinition.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceDialect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::akao_snes {

inline constexpr std::string_view kAkaoSnesFormatName = "AkaoSnes";
inline constexpr auto kAkaoSnesSequenceDialectId = "akao-snes";

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

struct AkaoSnesProfile {
  AkaoSnesVersion version = AKAOSNES_NONE;
  AkaoSnesMinorVersion minorVersion = AKAOSNES_NOMINORVERSION;
};

[[nodiscard]] constexpr u32 encodeAkaoSnesProfile(AkaoSnesProfile profile) {
  return static_cast<u32>(profile.version) | (static_cast<u32>(profile.minorVersion) << 8);
}

[[nodiscard]] constexpr AkaoSnesProfile decodeAkaoSnesProfile(u32 value) {
  return AkaoSnesProfile{
      .version = static_cast<AkaoSnesVersion>(value & 0xff),
      .minorVersion = static_cast<AkaoSnesMinorVersion>((value >> 8) & 0xff),
  };
}

inline constexpr u32 kAkaoSnesAramSize = 0x10000;
inline constexpr u32 kAkaoSnesMaxTracks = 8;
inline constexpr u16 kAkaoSnesPpqn = 48;
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

[[nodiscard]] constexpr bool akaoSnesUses8BitPan(AkaoSnesProfile profile) {
  return akaoSnesUses8BitPan(profile.version, profile.minorVersion);
}

[[nodiscard]] constexpr double akaoSnesFrameRateHz(u8 timer0Frequency) {
  return 8000.0 / timer0Frequency;
}

[[nodiscard]] inline double akaoSnesVibratoDepthCentsForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }
  const double ratio = 15.0 * amplitude / 32768.0;
  return std::max(1200.0 * std::log2(1.0 + ratio), -1200.0 * std::log2(1.0 - ratio));
}

[[nodiscard]] inline double akaoSnesV1VibratoDepthCents(u8 amplitude) {
  return amplitude == 0 ? 0.0 : 1200.0 * std::log2(1.0 + (static_cast<double>(amplitude) / 3072.0));
}

[[nodiscard]] inline double akaoSnesTremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }
  return -20.0 * std::log10(std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0)));
}

[[nodiscard]] constexpr bool akaoSnesExportsTremolo(AkaoSnesVersion version) {
  return version == AKAOSNES_V3 || version == AKAOSNES_V4;
}

[[nodiscard]] constexpr u32 akaoSnesSequenceHeaderSize(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion) {
  return version == AKAOSNES_V3   ? (minorVersion == AKAOSNES_V3_FFMQ ? 18 : 20)
         : version == AKAOSNES_V4 ? 20
                                  : kAkaoSnesMaxTracks * 2;
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

struct AkaoSnesLayout {
  AkaoSnesVersion version = AKAOSNES_NONE;
  AkaoSnesMinorVersion minorVersion = AKAOSNES_NOMINORVERSION;
  u32 sequenceHeaderAddress = 0;
  u32 apuRelocBase = 0;
  std::optional<u32> spcDirAddress;
  std::optional<u32> tuningTableAddress;
  std::optional<u32> adsrTableAddress;
  std::optional<u32> percussionTableAddress;
  std::optional<u32> volumeEnvelopeTableAddress;
};

struct AkaoSnesTrackDecodeOptions {
  AkaoSnesProfile profile;
  u32 sourceTrackNumber = 0;
  u32 startAddress = 0;
  u32 bytecodeEnd = std::numeric_limits<u32>::max();
  u32 romRelocBase = 0;
  u32 apuRelocBase = 0;
  std::optional<core::AssetId> sequenceAsset;
  std::optional<core::SourceAnnotationId> parentAnnotation;
  core::SourceMapBuilder* sourceMap = nullptr;
  std::vector<core::Diagnostic>* diagnostics = nullptr;
};

[[nodiscard]] std::optional<AkaoSnesLayout> findAkaoSnesLayout(core::ByteReader reader);
[[nodiscard]] const core::SequenceDialect& akaoSnesSequenceDialect();
[[nodiscard]] core::TrackProgram decodeAkaoSnesSourceTrack(core::ByteReader reader,
                                                           const AkaoSnesTrackDecodeOptions& options);
[[nodiscard]] core::SequenceProgram parseAkaoSnesSequence(core::ByteReader reader, const AkaoSnesLayout& layout,
                                                          core::AssetId sequenceId,
                                                          core::SourceMapBuilder* sourceMap = nullptr,
                                                          std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::optional<core::ScanSynthRefs> addAkaoSnesSynth(core::ScanResultBuilder& builder,
                                                                  const AkaoSnesLayout& layout,
                                                                  std::string_view displayName);
[[nodiscard]] core::FormatDefinition akaoSnesDefinition();

}  // namespace vgmtrans::formats::akao_snes

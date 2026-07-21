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

[[nodiscard]] constexpr double akaoSnesFrameRateHz(u8 timer0Frequency) {
  return 8000.0 / timer0Frequency;
}

struct AkaoSnesLfoRateRange {
  double minimum = 1.0 / 16.0;
  double maximum = 0.0;
};

[[nodiscard]] constexpr AkaoSnesLfoRateRange akaoSnesLfoRateRange(AkaoSnesVersion version) {
  constexpr u8 minimumTimerFrequency = 0x24;
  return AkaoSnesLfoRateRange{
      .maximum = akaoSnesFrameRateHz(minimumTimerFrequency) / (version == AKAOSNES_V1 ? 4.0 : 2.0),
  };
}

[[nodiscard]] constexpr double akaoSnesMaxLfoDelaySeconds(AkaoSnesVersion version) {
  constexpr double v1 = 254.0 * 256.0 / (8000.0 / 0x24);
  constexpr double v4 = 254.0 * 256.0 / (8000.0 / 0x2a);
  constexpr double other = 255.0 * 256.0 / (8000.0 / 0x2a);
  return version == AKAOSNES_V1 ? v1 : (version == AKAOSNES_V4 ? v4 : other);
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
[[nodiscard]] bool addAkaoSnesSynth(core::ScanResultBuilder& builder, core::ScanInstrumentSetRef instrumentSet,
                                    core::ScanSampleCollectionRef sampleCollection, const AkaoSnesLayout& layout,
                                    std::string_view displayName);
[[nodiscard]] core::FormatDefinition akaoSnesDefinition();

}  // namespace vgmtrans::formats::akao_snes

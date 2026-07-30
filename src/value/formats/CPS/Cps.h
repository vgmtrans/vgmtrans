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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::cps {

inline constexpr std::string_view kCpsFormatName = "CPS";
inline constexpr std::string_view kCps1Ym2151Domain = "cps1:ym2151";
inline constexpr std::string_view kCps1OkiDomain = "cps1:oki";
inline constexpr std::string_view kCpsQSoundDomain = "cps:qsound";
inline constexpr std::string_view kCps1V1DialectId = "cps:cps1-v1";
inline constexpr std::string_view kCpsEarlyDialectId = "cps:early";
inline constexpr std::string_view kCpsLateDialectId = "cps:late";
inline constexpr u32 kCpsPpqn = 48;
inline constexpr double kCps2DriverRateHertz = 62.5;
inline constexpr double kCps3DriverRateHertz = 59.599491;

enum class CpsVersion : u8 {
  Unknown,
  Cps1V100,
  Cps1V200,
  Cps1V350,
  Cps1V425,
  Cps1V500,
  Cps1V502,
  Cps2V100,
  Cps2V101,
  Cps2V103,
  Cps2V104,
  Cps2V105A,
  Cps2V105C,
  Cps2V105,
  Cps2V106B,
  Cps2V115C,
  Cps2V115,
  Cps2V116B,
  Cps2V116,
  Cps2V130,
  Cps2V131,
  Cps2V140,
  Cps2V171,
  Cps2V180,
  Cps2V200,
  Cps2V201B,
  Cps2V210,
  Cps2V211,
  Cps3,
};

struct CpsSequenceInfo {
  u32 index = 0;
  u32 offset = 0;
  core::SourceRange pointer;
  std::string name;
};

struct CpsLayout {
  CpsVersion version = CpsVersion::Unknown;
  std::string game;
  core::SourceRange program;
  core::SourceRange sampleRom;
  u32 sequenceTableOffset = 0;
  u32 sequenceTableLength = 0;
  u32 instrumentTableOffset = 0;
  u32 instrumentTableLength = 0;
  u32 sampleInfoTableOffset = 0;
  u32 sampleInfoTableLength = 0;
  std::optional<u32> articulationTableOffset;
  u32 articulationTableLength = 0;
  u32 instrumentBanks = 0;
  u8 masterVolume = 127;
  std::optional<u32> cps1SampleInstrumentTableOffset;
  std::vector<s8> cps1InstrumentTransposes;
  std::vector<CpsSequenceInfo> sequences;
};

[[nodiscard]] std::optional<CpsVersion> cpsVersion(std::string_view value);
[[nodiscard]] const char* cpsVersionName(CpsVersion version);
[[nodiscard]] bool isCps1(CpsVersion version);
[[nodiscard]] bool isCps3(CpsVersion version);
[[nodiscard]] bool usesLateSequence(CpsVersion version);
[[nodiscard]] double cpsDriverRateHertz(CpsVersion version);
[[nodiscard]] std::optional<u32> cpsSequenceAddress(const CpsLayout& layout, u32 encodedPointer);
[[nodiscard]] constexpr double cpsVolumeAdjustmentGain(s32 adjustment) noexcept {
  return static_cast<double>(static_cast<u32>(adjustment + 64) & 0x7fU) / 64.0;
}

[[nodiscard]] std::optional<CpsLayout> findCpsLayout(const core::SourceFile& source, core::ByteReader reader,
                                                     std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] core::SequenceProgram decodeCpsSequence(core::ByteReader reader, const CpsLayout& layout,
                                                      const CpsSequenceInfo& sequence, core::AssetId sequenceAsset,
                                                      core::SourceMapBuilder* sourceMap = nullptr,
                                                      std::vector<core::Diagnostic>* diagnostics = nullptr);

struct Cps1SynthRefs {
  std::optional<core::ScanInstrumentSetRef> ym2151;
  std::optional<core::ScanSynthRefs> oki;
};

[[nodiscard]] Cps1SynthRefs addCps1Synth(core::ScanResultBuilder& builder, CpsLayout& layout);
[[nodiscard]] core::ScanSynthRefs addCpsQSoundSynth(core::ScanResultBuilder& builder, const CpsLayout& layout);

[[nodiscard]] const core::SequenceDialect& cps1V1Dialect();
[[nodiscard]] const core::SequenceDialect& cpsEarlyDialect();
[[nodiscard]] const core::SequenceDialect& cpsLateDialect();
[[nodiscard]] core::FormatDefinition cpsDefinition();

}  // namespace vgmtrans::formats::cps

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/InstrumentIdentity.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/SourceExtractor.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::triace_ps1 {

inline constexpr std::string_view kTriAcePs1FormatName = "TriAcePS1";
inline constexpr std::string_view kTriAcePs1ImageFormat = "TriAcePS1Image";
inline constexpr std::string_view kTriAcePs1InstrumentDomain = "triace-ps1.instrument";
inline constexpr std::string_view kTriAcePs1CommandKindPrefix = "triace-ps1:sequence";

[[nodiscard]] inline core::InstrumentIdentity triAcePs1InstrumentIdentity(u8 bank, u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kTriAcePs1InstrumentDomain),
      .key = (static_cast<u32>(bank) << 8) | program,
  };
}

struct TriAcePs1TrackLayout {
  u8 slot = 0;
  u32 recordOffset = 0;
  u16 unknown1 = 0;
  u16 unknown2 = 0;
  u32 playlistOffset = 0;
  u32 playlistLength = 0;
  std::vector<u32> patternAddresses;
};

struct TriAcePs1SequenceLayout {
  u32 offset = 0;
  u32 length = 0;
  u8 tempo = 0;
  u8 timeSignatureNumerator = 0;
  u8 timeSignatureDenominator = 0;
  std::vector<TriAcePs1TrackLayout> tracks;
};

struct TriAcePs1BankLayout {
  u32 offset = 0;
  u32 length = 0;
  u16 instrumentSectionSize = 0;
  u16 unknown06 = 0;
  u16 unknown08 = 0;
  u16 unknown0a = 0;
  u32 sampleSectionOffset = 0;
  u32 sampleSectionSize = 0;
};

[[nodiscard]] std::optional<TriAcePs1SequenceLayout> readTriAcePs1SequenceLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<TriAcePs1SequenceLayout> findTriAcePs1Sequences(core::ByteReader reader);
[[nodiscard]] std::optional<TriAcePs1BankLayout> readTriAcePs1BankLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<TriAcePs1BankLayout> findTriAcePs1Banks(core::ByteReader reader, u32 begin = 0,
                                                                  std::optional<u32> length = std::nullopt);

[[nodiscard]] std::optional<core::ScanSoundBankDraft> addTriAcePs1Bank(core::ScanResultBuilder& result,
                                                                       const TriAcePs1BankLayout& layout);
[[nodiscard]] core::SequenceProgram parseTriAcePs1Sequence(core::ByteReader reader, core::AssetId id,
                                                           const TriAcePs1SequenceLayout& layout,
                                                           core::SourceMapBuilder* sourceMap = nullptr,
                                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& triAcePs1SequenceConfig();
[[nodiscard]] core::SourceExtractor triAcePs1Extractor();
[[nodiscard]] core::FormatModule triAcePs1Module();

}  // namespace vgmtrans::formats::triace_ps1

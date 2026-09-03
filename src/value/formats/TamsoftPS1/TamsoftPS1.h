/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/InstrumentIdentity.h"
#include "value/scan/CollectionDiscovery.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::tamsoft_ps1 {

inline constexpr std::string_view kFormatName = "TamsoftPS1";
inline constexpr std::string_view kCommandKindPrefix = "tamsoft-ps1:sequence";
inline constexpr std::string_view kInstrumentDomain = "tamsoft-ps1.instrument";

enum class Generation : u8 {
  Ps1,
  Ps2,
};

[[nodiscard]] inline core::InstrumentIdentity instrumentIdentity(u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kInstrumentDomain),
      .key = program,
  };
}

struct TrackLayout {
  u32 slot = 0;
  u32 headerOffset = 0;
  u32 offset = 0;
  u8 priority = 1;
};

struct SequenceLayout {
  u32 song = 0;
  u16 type = 0;
  u32 tableSize = 0;
  u32 headerOffset = 0;
  u32 headerSize = 0;
  Generation generation = Generation::Ps1;
  std::vector<TrackLayout> tracks;
};

struct BankLayout {
  u32 sampleSize = 0;
  Generation generation = Generation::Ps1;
};

struct SequenceData {
  std::string stem;
  u32 song = 0;
  Generation generation = Generation::Ps1;
  bool usesMusicBank = false;
};

struct BankData {
  std::string stem;
  Generation generation = Generation::Ps1;
};

[[nodiscard]] std::vector<SequenceLayout> readSequenceLayouts(core::ByteReader reader);
[[nodiscard]] std::optional<BankLayout> readBankLayout(core::ByteReader reader);
[[nodiscard]] core::SequenceProgram parseSequence(core::ByteReader reader, core::AssetId id,
                                                  const SequenceLayout& layout,
                                                  core::SourceMapBuilder* sourceMap = nullptr,
                                                  std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] bool addBank(core::ScanResultBuilder& result, const BankLayout& layout, std::string_view name);
[[nodiscard]] std::vector<core::DesiredCollection> resolveCollections(
    const core::CollectionDiscoveryContext& context);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::tamsoft_ps1

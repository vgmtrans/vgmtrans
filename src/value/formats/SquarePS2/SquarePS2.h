/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::square_ps2 {

inline constexpr std::string_view kSquarePs2FormatName = "SquarePS2";
inline constexpr std::string_view kSquarePs2InstrumentDomain = "square-ps2.instrument";
inline constexpr std::string_view kSquarePs2CommandKindPrefix = "square-ps2:sequence";

[[nodiscard]] inline core::InstrumentIdentity instrumentIdentity(u16 bank, u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kSquarePs2InstrumentDomain),
      .key = (static_cast<u32>(bank) << 8) | program,
  };
}

struct BgmTrackLayout {
  u32 blockOffset = 0;
  u32 dataOffset = 0;
  u32 length = 0;
};

struct BgmLayout {
  u32 offset = 0;
  u32 length = 0;
  u32 declaredLength = 0;
  u16 sequenceId = 0;
  u16 waveBankId = 0;
  u8 trackCount = 0;
  u16 initialTempo = 120;
  u8 initialMasterLevel = 127;
  u16 ppqn = 48;
  u32 flags = 0;
  std::vector<BgmTrackLayout> tracks;
};

struct WdLayout {
  u32 offset = 0;
  u32 length = 0;
  u16 bankId = 0;
  u32 sampleSize = 0;
  u32 instrumentCount = 0;
  u32 regionCount = 0;
  u32 instrumentTableOffset = 0;
  u32 sampleOffset = 0;
};

struct EnvelopeDefaults {
  u16 bank = 0;
  u8 program = 0;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
};

struct SequenceData {
  u16 waveBankId = 0;
};

struct SoundBankData {
  u16 bankId = 0;
  std::vector<EnvelopeDefaults> envelopes;
};

struct RuntimeConfig {
  u16 defaultBank = 0;
  std::vector<EnvelopeDefaults> envelopes;
};

[[nodiscard]] std::optional<BgmLayout> readBgmLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<BgmLayout> findBgmLayouts(core::ByteReader reader);
[[nodiscard]] std::optional<WdLayout> readWdLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<WdLayout> findWdLayouts(core::ByteReader reader);

[[nodiscard]] core::SequenceProgram parseBgm(core::ByteReader reader, core::AssetId id, const BgmLayout& layout,
                                             core::SourceMapBuilder* sourceMap = nullptr,
                                             std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] core::SequenceRuntime sequenceRuntime(RuntimeConfig config);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();

[[nodiscard]] std::optional<core::ScanSoundBankDraft> addWd(core::ScanResultBuilder& result, const WdLayout& layout);
[[nodiscard]] std::vector<core::DesiredCollection> resolveCollections(const core::CollectionDiscoveryContext& context);
void bindCollection(core::CollectionBindingContext& context);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::square_ps2

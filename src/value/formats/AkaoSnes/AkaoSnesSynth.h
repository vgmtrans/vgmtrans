/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/formats/AkaoSnes/AkaoSnesLayout.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/ScanTypes.h"

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::akao_snes {

struct AkaoSnesInstrumentInfo {
  u8 srcn = 0;
  u32 tuningAddress = 0;
  u32 adsrAddress = 0;
  u8 tuning1 = 0;
  u8 tuning2 = 0;
  u8 adsr1 = 0xff;
  u8 adsr2 = 0xe0;
  bool percussion = false;
  u8 percussionIndex = 0;
  u8 percussionKey = 0;
  std::optional<u8> percussionPan;
};

struct AkaoSnesSampleInfo {
  u8 srcn = 0;
  u32 dirEntryAddress = 0;
  u32 startAddress = 0;
  u32 loopAddress = 0;
  u32 encodedLength = 0;
  bool loops = false;
};

[[nodiscard]] std::vector<AkaoSnesInstrumentInfo> parseAkaoSnesInstrumentInfos(core::ByteReader reader,
                                                                               const AkaoSnesLayout& layout);

[[nodiscard]] std::vector<AkaoSnesSampleInfo> parseAkaoSnesSampleInfos(
    core::ByteReader reader, u32 spcDirAddress, const std::vector<AkaoSnesInstrumentInfo>& instruments);

[[nodiscard]] core::SampleCollectionAsset parseAkaoSnesSamples(const core::ScanInput& input,
                                                               core::AssetId sampleCollectionId,
                                                               const std::vector<AkaoSnesSampleInfo>& sampleInfos,
                                                               std::string_view displayName,
                                                               core::SourceMapBuilder* sourceMap = nullptr);

[[nodiscard]] core::InstrumentSetAsset parseAkaoSnesInstrumentSet(
    const core::ScanInput& input, core::ScanResultBuilder& builder, core::AssetId instrumentSetId,
    core::ScanSampleCollectionRef sampleCollection, const AkaoSnesLayout& layout,
    const std::vector<AkaoSnesInstrumentInfo>& instrumentInfos, const std::vector<AkaoSnesSampleInfo>& sampleInfos,
    std::string_view displayName);

}  // namespace vgmtrans::formats::akao_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/FormatModule.h"

#include <string_view>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

struct CapcomSnesSampleInfo {
  u8 srcn = 0;
  u32 dirEntryAddress = 0;
  u32 startAddress = 0;
  u32 loopAddress = 0;
  u32 encodedLength = 0;
  bool loops = false;
};

struct CapcomSnesInstrumentInfo {
  u32 index = 0;
  u32 address = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  s16 pitchScale = 0;
};

[[nodiscard]] std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(
    core::ByteReader reader,
    u32 instrumentTableAddress,
    u32 spcDirAddress);

[[nodiscard]] std::vector<CapcomSnesSampleInfo> parseCapcomSnesSampleInfos(
    core::ByteReader reader,
    u32 spcDirAddress,
    const std::vector<CapcomSnesInstrumentInfo>& instruments);

[[nodiscard]] core::SampleCollectionAsset parseCapcomSnesSamples(
    const core::ScanInput& input,
    core::AssetId sampleCollectionId,
    const std::vector<CapcomSnesSampleInfo>& sampleInfos,
    std::string_view displayName);

[[nodiscard]] core::InstrumentSetAsset parseCapcomSnesInstrumentSet(
    const core::ScanInput& input,
    core::AssetId instrumentSetId,
    core::AssetId sampleCollectionId,
    const std::vector<CapcomSnesInstrumentInfo>& instrumentInfos,
    const std::vector<CapcomSnesSampleInfo>& sampleInfos,
    std::string_view displayName);

}  // namespace vgmtrans::formats::capcom_snes

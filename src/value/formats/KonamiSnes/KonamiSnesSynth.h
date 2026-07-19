/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/formats/KonamiSnes/KonamiSnesLayout.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/scan/ScanResultBuilder.h"

#include <string_view>
#include <vector>

namespace vgmtrans::formats::konami_snes {

struct KonamiSnesInstrumentInfo {
  u32 index = 0;
  u32 address = 0;
  u8 srcn = 0;
  s8 key = 0;
  s8 tuning = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u8 pan = 0;
  u8 volume = 0;
  bool percussion = false;
  u8 percussionNote = 0;
};

[[nodiscard]] std::vector<KonamiSnesInstrumentInfo> parseKonamiSnesInstrumentInfos(core::ByteReader reader,
                                                                                   const KonamiSnesLayout& layout);

[[nodiscard]] core::SnesBrrCatalog parseKonamiSnesSampleInfos(core::ByteReader reader, u32 spcDirAddress,
                                                              const std::vector<KonamiSnesInstrumentInfo>& instruments);

[[nodiscard]] bool addKonamiSnesSynth(const core::ScanInput& input, core::ScanResultBuilder& builder,
                                      core::ScanInstrumentSetRef instrumentSet,
                                      core::ScanSampleCollectionRef sampleCollection, const KonamiSnesLayout& layout,
                                      std::string_view displayName);

}  // namespace vgmtrans::formats::konami_snes

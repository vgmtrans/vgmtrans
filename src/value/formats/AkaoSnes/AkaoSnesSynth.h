/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/formats/AkaoSnes/AkaoSnesLayout.h"
#include "value/scan/ScanResultBuilder.h"

#include <string_view>

namespace vgmtrans::formats::akao_snes {

[[nodiscard]] bool addAkaoSnesSynth(const core::ScanInput& input, core::ScanResultBuilder& builder,
                                    core::ScanInstrumentSetRef instrumentSet,
                                    core::ScanSampleCollectionRef sampleCollection, const AkaoSnesLayout& layout,
                                    std::string_view displayName);

}  // namespace vgmtrans::formats::akao_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/InstrumentVariants.h"

namespace vgmtrans::core {

using DynamicEnvelopeMaterialization = InstrumentVariantMaterialization;

// Compatibility entry point for callers that only materialize dynamic ADSR.
[[nodiscard]] inline DynamicEnvelopeMaterialization materializeDynamicEnvelopes(const PerformanceSequence& performance,
                                                                                std::span<SoundBankAsset> soundBanks) {
  return materializeInstrumentVariants(performance, soundBanks, InstrumentVariantOptions{.dynamicEnvelopes = true});
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/synth/SynthModel.h"

#include <span>
#include <vector>

namespace vgmtrans::core {

struct DynamicEnvelopeMaterialization {
  // Export-only performance with generated instrument selections inserted
  // immediately before the fresh attacks that need them.
  PerformanceSequence performance;
  std::vector<Diagnostic> diagnostics;
};

// Appends only the instrument variants used by fresh note attacks. The returned
// performance is export-only; the input performance remains unchanged.
[[nodiscard]] DynamicEnvelopeMaterialization materializeDynamicEnvelopes(const PerformanceSequence& performance,
                                                                         std::span<InstrumentSetAsset> instrumentSets);

}  // namespace vgmtrans::core

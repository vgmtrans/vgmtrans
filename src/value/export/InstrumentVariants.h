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

struct InstrumentVariantOptions {
  bool dynamicEnvelopes = false;
  bool signedStereo = false;
};

struct InstrumentVariantMaterialization {
  PerformanceSequence performance;
  std::vector<Diagnostic> diagnostics;
};

// Appends only variants selected by fresh note attacks. Applying all requested
// state in one pass keeps combined envelope and stereo changes on one variant;
// the input performance remains unchanged.
[[nodiscard]] InstrumentVariantMaterialization materializeInstrumentVariants(const PerformanceSequence& performance,
                                                                             std::span<SoundBankAsset> soundBanks,
                                                                             InstrumentVariantOptions options);

}  // namespace vgmtrans::core

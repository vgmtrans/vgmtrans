/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/validation/ValidationReport.h"

namespace vgmtrans::core {

struct InstrumentSetAsset;
struct SampleCollectionAsset;

// Checks synth values whose meaning is independent of SF2, DLS, or another export target.
[[nodiscard]] ValidationReport validateInstrumentSet(const InstrumentSetAsset& instrumentSet);
[[nodiscard]] ValidationReport validateSampleCollection(const SampleCollectionAsset& sampleCollection);

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/validation/ValidationReport.h"

namespace vgmtrans::core {

struct SoundBankAsset;
struct SamplePoolAsset;

// Checks synth values whose meaning is independent of SF2, DLS, or another export target.
[[nodiscard]] ValidationReport validateSoundBank(const SoundBankAsset& soundBank);
[[nodiscard]] ValidationReport validateSamplePool(const SamplePoolAsset& samplePool);

}  // namespace vgmtrans::core

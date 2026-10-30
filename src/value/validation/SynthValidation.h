/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/validation/ValidationReport.h"

#include <span>

namespace vgmtrans::core {

struct SoundBankAsset;
struct SamplePoolAsset;

// Checks synth values whose meaning is independent of SF2, DLS, or another export target.
[[nodiscard]] ValidationReport validateSoundBank(const SoundBankAsset& soundBank);
[[nodiscard]] ValidationReport validateSamplePool(const SamplePoolAsset& samplePool);
// externalPools is the complete set of nonlocal sample owners permitted here.
[[nodiscard]] ValidationReport validateSampleReferences(
    const SoundBankAsset& soundBank, std::span<const SamplePoolAsset* const> externalPools,
    bool allowUnboundSampleReferences = false);

}  // namespace vgmtrans::core

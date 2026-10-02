/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"

#include <optional>

namespace vgmtrans::core {

struct SequenceModulationProfile;

struct MidiModulationUsage {
  // Maxima in [0, 1] for the whole sequence, before MIDI quantization.
  // Empty means unobserved; zero means observed but inactive.
  std::optional<double> vibratoDepth;
  std::optional<double> vibratoRate;
  std::optional<double> tremoloDepth;
  std::optional<double> tremoloRate;
};

[[nodiscard]] bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept;
[[nodiscard]] MidiModulationUsage analyzePerformanceModulationUsage(
    const PerformanceSequence& sequence, const SequenceModulationProfile* modulationProfile = nullptr);

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"

namespace vgmtrans::core {

struct SoundBankAsset;

// One physical LFO description derived from the rendered song. The same
// description drives MIDI controller normalization and synth construction.
struct SequenceModulationProfile {
  InstrumentModulation instruments;
  double maxPanDepth = 0.0;
  std::optional<ModulationRange> panRateHertz;

  [[nodiscard]] bool hasSynthModulation() const noexcept {
    return instruments.vibrato.has_value() || instruments.tremolo.has_value();
  }
  [[nodiscard]] bool empty() const noexcept { return !hasSynthModulation() && maxPanDepth <= 0.0 && !panRateHertz; }
};

[[nodiscard]] SequenceModulationProfile analyzeSequenceModulation(const PerformanceSequence& sequence);

// Resolves a physical event into the normalized input used by MIDI synth
// controls. Events without a physical representation use their normalized
// source control amount.
[[nodiscard]] double modulationControllerAmount(const ModulationPerformanceEvent& event,
                                                const SequenceModulationProfile* profile) noexcept;
[[nodiscard]] u8 vibratoDelayControllerValue(const VibratoDelayPerformanceEvent& event,
                                             const SequenceModulationProfile* profile) noexcept;
[[nodiscard]] u8 tremoloDelayControllerValue(const TremoloDelayPerformanceEvent& event,
                                             const SequenceModulationProfile* profile) noexcept;

// Adds the song-derived LFO behavior to every playable instrument in a
// collection. Targets absent from the profile are left unchanged.
void applySequenceModulation(SoundBankAsset& soundBank, const SequenceModulationProfile& profile);

}  // namespace vgmtrans::core

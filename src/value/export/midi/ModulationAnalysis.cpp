/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/ModulationAnalysis.h"

#include "value/export/SequenceModulationProfile.h"

#include <algorithm>

namespace vgmtrans::core {

namespace {

void observe(std::optional<double>& maximum, double normalized) {
  maximum = std::max(maximum.value_or(0.0), std::clamp(normalized, 0.0, 1.0));
}

void observePerformanceModulation(MidiModulationUsage& usage, const ModulationPerformanceEvent& event,
                                  const SequenceModulationProfile* profile) {
  const double amount = modulationControllerAmount(event, profile);
  switch (event.target) {
    case ModulationPerformanceTarget::VibratoDepth:
      observe(usage.vibratoDepth, amount);
      break;
    case ModulationPerformanceTarget::VibratoRate:
      observe(usage.vibratoRate, amount);
      break;
    case ModulationPerformanceTarget::TremoloDepth:
      observe(usage.tremoloDepth, amount);
      break;
    case ModulationPerformanceTarget::TremoloRate:
      observe(usage.tremoloRate, amount);
      break;
    case ModulationPerformanceTarget::PanDepth:
    case ModulationPerformanceTarget::PanRate:
      // MIDI has no standard pan-LFO controller pair. These targets are
      // retained for sequence-event simulation instead.
      break;
  }
}

}  // namespace

bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept {
  return usage.vibratoDepth || usage.vibratoRate || usage.tremoloDepth || usage.tremoloRate;
}

MidiModulationUsage analyzePerformanceModulationUsage(const PerformanceSequence& sequence,
                                                      const SequenceModulationProfile* modulationProfile) {
  std::optional<SequenceModulationProfile> derivedModulationProfile;
  if (modulationProfile == nullptr) {
    derivedModulationProfile = analyzeSequenceModulation(sequence);
    modulationProfile = &*derivedModulationProfile;
  }

  MidiModulationUsage result;
  for (const auto& track : sequence.tracks) {
    for (const auto& event : track.events) {
      if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
        observePerformanceModulation(result, *modulation, modulationProfile);
      }
    }
  }

  return result;
}

}  // namespace vgmtrans::core

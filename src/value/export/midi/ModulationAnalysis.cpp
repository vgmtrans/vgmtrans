/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/ModulationAnalysis.h"

#include "value/export/SequenceModulationProfile.h"

#include <algorithm>
#include <cmath>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u8 midiControllerValue(double normalized) {
  return static_cast<u8>(
      std::clamp<int>(static_cast<int>(std::lround(std::clamp(normalized, 0.0, 1.0) * 127.0)), 0, 127));
}

void observe(std::optional<MidiModulationMaximum>& maximum, double normalized) {
  const double clampedNormalized = std::clamp(normalized, 0.0, 1.0);
  const u8 controller = midiControllerValue(clampedNormalized);
  if (!maximum) {
    maximum = MidiModulationMaximum{.controllerValue = controller, .normalized = clampedNormalized};
    return;
  }

  maximum->controllerValue = std::max(maximum->controllerValue, controller);
  maximum->normalized = std::max(maximum->normalized, clampedNormalized);
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
  if (modulationProfile == nullptr &&
      std::ranges::any_of(sequence.tracks, &PerformanceTrack::hasPhysicalModulation)) {
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

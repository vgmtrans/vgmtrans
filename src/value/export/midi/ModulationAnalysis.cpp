/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/ModulationAnalysis.h"

#include "value/export/SequenceModulationProfile.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u32 midiControllerValue(double normalized);

void observe(ObservedValueRange& range, u32 value, double normalized, SourceRange sourceRange) {
  const double clampedNormalized = std::clamp(normalized, 0.0, 1.0);
  if (!range.observed) {
    range.observed = true;
    range.min = value;
    range.max = value;
    range.normalizedMin = clampedNormalized;
    range.normalizedMax = clampedNormalized;
    if (sourceRange.valid()) {
      range.firstRange = sourceRange;
    }
    return;
  }

  range.min = std::min(range.min, value);
  range.max = std::max(range.max, value);
  range.normalizedMin = std::min(range.normalizedMin, clampedNormalized);
  range.normalizedMax = std::max(range.normalizedMax, clampedNormalized);
}

void merge(ObservedValueRange& destination, const ObservedValueRange& source) {
  if (!source.observed) {
    return;
  }
  if (!destination.observed) {
    destination = source;
    return;
  }
  destination.min = std::min(destination.min, source.min);
  destination.max = std::max(destination.max, source.max);
  destination.normalizedMin = std::min(destination.normalizedMin, source.normalizedMin);
  destination.normalizedMax = std::max(destination.normalizedMax, source.normalizedMax);
}

[[nodiscard]] u32 midiControllerValue(double normalized) {
  return static_cast<u32>(
      std::clamp<int>(static_cast<int>(std::lround(std::clamp(normalized, 0.0, 1.0) * 127.0)), 0, 127));
}

void observePerformanceModulation(MidiTrackModulationUsage& usage, const ModulationPerformanceEvent& event,
                                  const SequenceModulationProfile* profile) {
  const double amount = modulationControllerAmount(event, profile);
  switch (event.target) {
    case ModulationPerformanceTarget::VibratoDepth:
      observe(usage.vibratoDepth, midiControllerValue(amount), amount, SourceRange{});
      break;
    case ModulationPerformanceTarget::VibratoRate:
      observe(usage.vibratoRate, midiControllerValue(amount), amount, SourceRange{});
      break;
    case ModulationPerformanceTarget::TremoloDepth:
      observe(usage.tremoloDepth, midiControllerValue(amount), amount, SourceRange{});
      break;
    case ModulationPerformanceTarget::TremoloRate:
      observe(usage.tremoloRate, midiControllerValue(amount), amount, SourceRange{});
      break;
    case ModulationPerformanceTarget::PanDepth:
    case ModulationPerformanceTarget::PanRate:
      // MIDI has no standard pan-LFO controller pair. These targets are
      // retained for sequence-event simulation instead.
      break;
  }
}

void mergeTrackUsage(MidiModulationUsage& result, const MidiTrackModulationUsage& trackUsage) {
  merge(result.vibratoDepth, trackUsage.vibratoDepth);
  merge(result.vibratoRate, trackUsage.vibratoRate);
  merge(result.tremoloDepth, trackUsage.tremoloDepth);
  merge(result.tremoloRate, trackUsage.tremoloRate);
}

}  // namespace

bool hasObservedValue(const ObservedValueRange& range) noexcept {
  return range.observed;
}

bool hasMidiModulationUsage(const MidiTrackModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.vibratoRate.observed || usage.tremoloDepth.observed ||
         usage.tremoloRate.observed;
}

bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.vibratoRate.observed || usage.tremoloDepth.observed ||
         usage.tremoloRate.observed;
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
  result.tracks.reserve(sequence.tracks.size());

  for (u32 trackIndex = 0; trackIndex < sequence.tracks.size(); ++trackIndex) {
    const auto& track = sequence.tracks[trackIndex];
    MidiTrackModulationUsage trackUsage{
        .trackIndex = trackIndex,
    };

    for (const auto& event : track.events) {
      if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
        observePerformanceModulation(trackUsage, *modulation, modulationProfile);
      }
    }

    mergeTrackUsage(result, trackUsage);
    result.tracks.push_back(std::move(trackUsage));
  }

  return result;
}

}  // namespace vgmtrans::core

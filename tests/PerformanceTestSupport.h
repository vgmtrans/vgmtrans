/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/export/midi/PerformanceMidiRenderer.h"

#include <variant>
#include <vector>

// Views borrow events from the track; keep the rendered performance alive.
template <class Event>
std::vector<const Event*> eventsOfType(const vgmtrans::core::PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const auto& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

// Fixture setup for tests whose subject is MIDI or format interpretation.
// Preparation/ownership tests call the production boundary directly.
inline vgmtrans::core::ResolvedPerformance prepareTestPerformance(
    const vgmtrans::core::PerformanceSequence& performance,
    std::span<const vgmtrans::core::SoundBankAsset* const> banks = {},
    vgmtrans::core::InstrumentPreparationOptions options = {}) {
  std::vector<vgmtrans::core::SoundBankAsset> copies;
  for (const auto* bank : banks) copies.push_back(bank ? *bank : vgmtrans::core::SoundBankAsset{});
  return vgmtrans::core::preparePerformance(performance, std::move(copies), options);
}

inline vgmtrans::core::MidiSequence renderTestMidi(
    const vgmtrans::core::PerformanceSequence& performance, vgmtrans::core::MidiExportOptions options = {},
    vgmtrans::core::ModulationConversionPolicy conversion = vgmtrans::core::ModulationConversionPolicy::SynthModulators,
    std::span<const vgmtrans::core::SoundBankAsset* const> banks = {},
    const vgmtrans::core::SequenceModulationProfile* modulation = nullptr) {
  const auto prepared = prepareTestPerformance(performance, banks);
  return vgmtrans::core::renderMidiSequence(prepared, options,
                                           conversion, modulation);
}

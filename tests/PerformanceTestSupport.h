/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"

#include <variant>
#include <vector>

// Own the emitter's counters and track together. Tests still use the production
// emitter directly; copying this fixture would leave it referring to the old track.
struct PerformanceTrackFixture {
  vgmtrans::core::PerformanceTrack track;
  u64 nextSequence = 0;
  u32 nextNote = 0, nextAutomation = 0;
  vgmtrans::core::PerformanceEmitter out;

  explicit PerformanceTrackFixture(u64 endTick = 0)
      : track{.id = vgmtrans::core::TrackId{0}, .endTick = endTick},
        out{track, {track.id, vgmtrans::core::CommandId{1}}, vgmtrans::core::SourceAnnotationId{2},
            0, nextSequence, nextNote, nextAutomation} {}
  PerformanceTrackFixture(const PerformanceTrackFixture&) = delete;
  PerformanceTrackFixture& operator=(const PerformanceTrackFixture&) = delete;
};

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

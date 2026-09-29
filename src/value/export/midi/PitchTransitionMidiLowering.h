/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/ResolvedPerformance.h"

namespace vgmtrans::core::detail {

// A note boundary still carries independent envelope/LFO decisions. Planning
// decides whether it emits a Note On and resolves that attack's entire duration.
// Continuations and expired boundaries remain in the timeline for controller timing.
enum class MidiNoteAction { Attack, Continue, Expired };

struct MidiNoteEvent : NotePerformanceEvent {
  // For Attack, the completed gate end includes extensions and the voice deadline.
  // The inherited durationTicks retains the source boundary's duration.
  u64 endTick = addTicks(header.tick, durationTicks);
  // A continuation bends relative to the attack it extends, not its requested key.
  double bendBaseKey = key;
  MidiNoteAction action = MidiNoteAction::Attack;
};

using MidiTrackEvent = PerformanceEventWithNote<MidiNoteEvent>;

// Temporary events for one MIDI track. Voice IDs and instrument handles refer
// to the read-only prepared input, which must remain alive through rendering.
// Source automations, inspection data, banks and address maps stay in that input.
[[nodiscard]] std::vector<MidiTrackEvent> lowerMidiTrackEvents(
    const ResolvedPerformance& performance, size_t trackIndex, const MidiExportOptions& options,
    const PerformanceTempoMap& tempos, std::vector<Diagnostic>& diagnostics);

}  // namespace vgmtrans::core::detail

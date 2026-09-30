/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/ResolvedPerformance.h"

namespace vgmtrans::core::detail {

// A completed physical attack. No source gate, continuation request or hardware
// deadline reaches the encoder; those decisions have already been applied.
struct MidiAttack {
  double key;
  double linearVelocity;
  u32 durationTicks;
  bool silencePreviousVoice;
};

// Live boundaries can change controllers without a Note On. Independent LFO
// requests are resolved from source defaults during planning. Expired boundaries
// apply no effects.
struct MidiNoteBoundary {
  PerformanceEventHeader header;
  std::optional<MidiAttack> attack;
  std::optional<ResolvedInstrument> instrument;
  bool restartVibrato = false;
  bool restartTremolo = false;
  bool restartPan = false;
  bool expired = false;
};

struct MidiInstrumentEvent {
  PerformanceEventHeader header;
  ResolvedInstrument selection;
  bool forceBankSelect = false;
};

// Controls retain their musical values until encoding. Source-only envelope
// updates and pitch-transition settings have already been consumed by preparation
// and lowering, respectively. Notes and instrument changes use completed decisions.
using MidiTrackEvent = std::variant<
    MidiNoteBoundary, MidiInstrumentEvent, LevelPerformanceEvent, ExpressionPerformanceEvent,
    PanPerformanceEvent, ChannelPanPerformanceEvent,
    StereoBalancePerformanceEvent, MasterLevelPerformanceEvent, ReverbPerformanceEvent, MonoModePerformanceEvent,
    TuningPerformanceEvent, PortamentoPerformanceEvent, PortamentoEnablePerformanceEvent, PitchBendPerformanceEvent,
    PitchBendRangePerformanceEvent,
    LegatoPedalPerformanceEvent, ModulationPerformanceEvent, MarkerPerformanceEvent>;

// Completed events for one MIDI track. Instrument pointers borrow the read-only
// prepared input, which must remain alive through rendering.
// Source automations, inspection data, banks and address maps stay in that input.
[[nodiscard]] std::vector<MidiTrackEvent> planMidiTrack(
    const ResolvedPerformance& performance, size_t trackIndex, const MidiExportOptions& options,
    const PerformanceTempoMap& tempos, std::vector<Diagnostic>& diagnostics);

}  // namespace vgmtrans::core::detail

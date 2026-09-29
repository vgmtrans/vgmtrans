/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/ResolvedPerformance.h"

namespace vgmtrans::core::detail {

// Temporary events for one MIDI track. Voice IDs and instrument handles refer
// to the read-only prepared input, which must remain alive through rendering.
// Source automations, inspection data, banks and address maps stay in that input.
[[nodiscard]] std::vector<PerformanceEvent> lowerMidiTrackEvents(
    const ResolvedPerformance& performance, size_t trackIndex, const MidiExportOptions& options,
    const PerformanceTempoMap& tempos, std::vector<Diagnostic>& diagnostics);

}  // namespace vgmtrans::core::detail

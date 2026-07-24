/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/sequence/PerformanceModel.h"

namespace vgmtrans::core {

// Resolves structured performance automation into the flat performance events
// consumed by the MIDI renderer. The returned value is temporary lowering
// state; the caller's target-neutral PerformanceSequence remains unchanged.
[[nodiscard]] PerformanceSequence lowerMidiPerformanceAutomation(const PerformanceSequence& performance,
                                                                 const MidiExportOptions& options);
[[nodiscard]] PerformanceSequence lowerMidiPerformanceAutomation(const PerformanceSequence& performance,
                                                                 const MidiExportOptions& options,
                                                                 const PerformanceTempoMap& tempos);

}  // namespace vgmtrans::core

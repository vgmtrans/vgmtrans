/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/ResolvedPerformance.h"

#include <span>

namespace vgmtrans::core {

// Lowering preserves resolved handles and ownership of their immutable banks.
// Taking by value lets callers retain the original or move disposable events.
[[nodiscard]] ResolvedPerformance lowerMidiPerformanceAutomation(
    ResolvedPerformance performance, const MidiExportOptions& options,
    const PerformanceTempoMap& tempos);

}  // namespace vgmtrans::core

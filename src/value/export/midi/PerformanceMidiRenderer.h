/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/midi/MidiModel.h"
#include "value/sequence/PerformanceModel.h"
#include "value/export/ExportTypes.h"

namespace vgmtrans::core {

class PerformanceMidiRenderer {
public:
  [[nodiscard]] MidiSequence render(const PerformanceSequence& performance, MidiExportOptions options = {}) const;
};

}  // namespace vgmtrans::core

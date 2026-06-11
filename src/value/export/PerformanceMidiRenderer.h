/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiModel.h"
#include "value/core/PerformanceModel.h"

namespace vgmtrans::core {

class PerformanceMidiRenderer {
public:
  [[nodiscard]] MidiSequence render(const PerformanceSequence& performance) const;
};

}  // namespace vgmtrans::core

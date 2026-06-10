/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/core/Model.h"

#include <vector>

namespace vgmtrans::core {

class MidiExporter {
 public:
  [[nodiscard]] std::vector<u8> exportMidi(const MidiSequence& sequence) const;
};

}  // namespace vgmtrans::core

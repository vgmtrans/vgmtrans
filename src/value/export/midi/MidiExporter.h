/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/export/midi/MidiModel.h"

#include <vector>

namespace vgmtrans::core {

// Writes the already-rendered MidiSequence as Standard MIDI File bytes. Driver
// interpretation should be finished before this class is called.
class MidiExporter {
 public:
  [[nodiscard]] std::vector<u8> exportMidi(const MidiSequence& sequence) const;
};

}  // namespace vgmtrans::core

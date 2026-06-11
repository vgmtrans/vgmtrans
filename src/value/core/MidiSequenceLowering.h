/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiSequenceProfile.h"

namespace vgmtrans::core {

[[nodiscard]] MidiSequence buildMidiSequence(const CommandSequence& commandSequence, const MidiSequenceProfile& profile,
                                             LoopPolicy loopPolicy);

}  // namespace vgmtrans::core

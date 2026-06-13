/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/bytecode/BytecodeSequenceDecoder.h"

#include <cstddef>

namespace vgmtrans::formats::nds {

[[nodiscard]] core::TrackProgram decodeMalformedSdatRangeTrack(core::ByteReader reader,
                                                               const core::BytecodeDispatchTable& dispatch,
                                                               const core::BytecodeCommandSpec& noOpSpec,
                                                               const core::BytecodeCommandSpec& terminalSpec,
                                                               u32 sequenceOffset, u32 sequenceEnd, u32 startOffset,
                                                               u32 trackIndex, size_t maxCommands);

}  // namespace vgmtrans::formats::nds

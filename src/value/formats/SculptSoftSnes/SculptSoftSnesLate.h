/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/SculptSoftSnes/SculptSoftSnesVoice.h"
#include "value/sequence/CommandSourceMap.h"

namespace vgmtrans::formats::sculpt_soft_snes {

[[nodiscard]] core::TrackProgram decodeLateTrack(const core::TrackDecodeScope& scope, const Layout& layout, u32 number,
                                                 u32 start, std::vector<core::Diagnostic>* diagnostics,
                                                 std::set<u8>* referencedPrograms);
[[nodiscard]] core::SequenceRuntime makeLateRuntime(RuntimeConfig config);

}  // namespace vgmtrans::formats::sculpt_soft_snes

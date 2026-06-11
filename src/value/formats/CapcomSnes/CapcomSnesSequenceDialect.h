/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SequenceDialect.h"
#include "value/core/Source.h"
#include "value/formats/CapcomSnes/CapcomSnesTypes.h"

namespace vgmtrans::formats::capcom_snes {

[[nodiscard]] core::SequenceDialect capcomSnesSequenceDialect(CapcomSnesEngineVersion version);
void registerCapcomSnesSequenceDialects(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeCapcomSnesSourceTrack(
    core::ByteReader reader,
    const core::SequenceDialect& dialect,
    u32 sourceTrackNumber,
    u32 startAddress);

}  // namespace vgmtrans::formats::capcom_snes

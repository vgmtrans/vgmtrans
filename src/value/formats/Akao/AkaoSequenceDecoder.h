/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/Akao/AkaoTypes.h"
#include "value/sequence/SequenceCursorDialect.h"

namespace vgmtrans::formats::akao {

[[nodiscard]] core::SequenceDialect makeAkaoDialect(AkaoPs1Version version);
[[nodiscard]] core::TrackProgram decodeAkaoTrack(core::ByteReader reader, const core::SequenceDialect& dialect,
                                                 core::CursorTrackDecodeInput input);

}  // namespace vgmtrans::formats::akao

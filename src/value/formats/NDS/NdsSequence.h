/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/formats/NDS/NdsTypes.h"
#include "value/sequence/SequenceDialect.h"

#include <vector>

namespace vgmtrans::formats::nds {

inline constexpr auto kNdsSequenceDialectId = "nds:sseq";

[[nodiscard]] const core::SequenceDialect& ndsSequenceDialect();

[[nodiscard]] core::SequenceProgram decodeNdsSequence(core::ByteReader reader, core::AssetId sequenceId,
                                                      NdsSequenceRange range,
                                                      core::SourceMapBuilder* sourceMap = nullptr,
                                                      std::vector<core::Diagnostic>* diagnostics = nullptr);

}  // namespace vgmtrans::formats::nds

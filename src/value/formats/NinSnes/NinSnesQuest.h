/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/NinSnes/NinSnes.h"

namespace vgmtrans::formats::nin_snes::quest {

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout,
                                           core::SectionPlaylist playlist, core::AssetId sequenceId,
                                           std::optional<core::SourceAnnotationId> parent,
                                           core::SourceMapBuilder* sourceMap,
                                           std::vector<core::Diagnostic>* diagnostics);

}  // namespace vgmtrans::formats::nin_snes::quest

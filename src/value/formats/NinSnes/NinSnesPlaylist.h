/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/NinSnes/NinSnes.h"

namespace vgmtrans::formats::nin_snes {

struct PlaylistDecode {
  core::SectionPlaylist playlist;
  std::optional<core::SourceAnnotationId> annotation;
};

[[nodiscard]] PlaylistDecode decodePlaylist(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                            core::SourceMapBuilder* sourceMap,
                                            std::vector<core::Diagnostic>* diagnostics);

}  // namespace vgmtrans::formats::nin_snes

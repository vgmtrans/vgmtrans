/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/Source.h"
#include "value/formats/CapcomSnes/CapcomSnesTypes.h"

#include <optional>
#include <string>

namespace vgmtrans::formats::capcom_snes {

inline constexpr u64 kCapcomSnesAramSize = 0x10000;
inline constexpr u32 kCapcomSnesMaxTracks = 8;
inline constexpr u32 kCapcomSnesPpqn = 48;

struct CapcomSnesLayout {
  // Addresses are ARAM offsets discovered from driver code patterns and DSP initialization.
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::none;
  bool hasSongList = false;
  bool bgmAtFixedAddress = false;
  u32 songListAddress = 0;
  u32 bgmHeaderAddress = 0;
  u32 sequenceHeaderAddress = 0;
  bool priorityInHeader = false;
  std::optional<u32> instrumentTableAddress;
  std::optional<u32> spcDirAddress;
};

[[nodiscard]] std::string capcomSnesSourceDisplayName(const core::SourceFile& source);
[[nodiscard]] std::optional<CapcomSnesLayout> findCapcomSnesLayout(core::ByteReader reader);

}  // namespace vgmtrans::formats::capcom_snes

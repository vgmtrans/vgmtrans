/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiSequenceProfile.h"

#include <string_view>

namespace vgmtrans::formats::capcom_snes {

enum class CapcomSnesEngineVersion : u8 {
  none,
  v1BgmInList,
  v2BgmUsuallyAtFixedLocation,
  v3BgmFixedLocation,
};

[[nodiscard]] std::string_view capcomSnesProfileName(CapcomSnesEngineVersion version);
[[nodiscard]] core::MidiSequenceProfile capcomSnesProfile(
    CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation);

void registerCapcomSnesProfile(core::MidiSequenceProfileRegistry& registry);

}  // namespace vgmtrans::formats::capcom_snes

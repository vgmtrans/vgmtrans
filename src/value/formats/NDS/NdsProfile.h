/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiSequenceProfile.h"

namespace vgmtrans::formats::nds {

inline constexpr auto kNdsProfileName = "NDS";

[[nodiscard]] core::MidiSequenceProfile ndsProfile();

void registerNdsProfile(core::MidiSequenceProfileRegistry& registry);

}  // namespace vgmtrans::formats::nds

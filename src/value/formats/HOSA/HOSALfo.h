/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"

namespace vgmtrans::formats::hosa {

[[nodiscard]] core::LfoShape vibratoShape(u8 waveform);

}  // namespace vgmtrans::formats::hosa

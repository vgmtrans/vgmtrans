#pragma once
#include <cstdint>

#include "Format.h"
#include "NDSScanner.h"
#include "VGMColl.h"

// *********
// NDSFormat
// *********

BEGIN_FORMAT(NDS)
  USING_SCANNER(NDSScanner)
END_FORMAT()

namespace nds {

inline constexpr double kPeriodicUpdateHz = 192.0;
inline constexpr double kLfoSpeedToHz = kPeriodicUpdateHz / 512.0;
inline constexpr double kVibratoBaseHz = 0.375;
inline constexpr double kVibratoMaxHz = 48.0;
inline constexpr double kVibratoDepthCents = 1200.0;
inline constexpr uint32_t kBaseTempo = 240;

} // namespace nds

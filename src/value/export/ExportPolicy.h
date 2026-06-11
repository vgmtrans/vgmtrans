/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

namespace vgmtrans::core {

// Controls whether synth modulators keep the full theoretical format range or
// are scaled to the controller values actually observed in the parsed sequence.
enum class ModulationScalingPolicy {
  FullFormatRange,
  ObservedSequenceRange,
};

}  // namespace vgmtrans::core

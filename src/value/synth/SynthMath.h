/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/base/Types.h"

namespace vgmtrans::core {

[[nodiscard]] double linearAmplitudeToAttenuationDb(double amplitude, double silenceDb = 96.0);
// Returns the dB-linear envelope duration that best approximates a
// linear-amplitude fade lasting secondsToSilence.
[[nodiscard]] double linearAmplitudeFadeToDbEnvelopeSeconds(double secondsToSilence);
[[nodiscard]] double panPositionFrom7Bit(u8 pan);

}  // namespace vgmtrans::core

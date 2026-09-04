/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"

namespace capcom_snes {

struct PanConversionResult {
  u8 midiPan = 64;
  double volumeScale = 1.0;
};

[[nodiscard]] u16 percentAmpTo14BitMidi(double percent);
[[nodiscard]] u8 percentAmpTo7BitMidi(double percent);

[[nodiscard]] PanConversionResult linear8BitPanToMidi(u8 biasedPan);
[[nodiscard]] PanConversionResult calculatePanV2(u8 biasedPan);

[[nodiscard]] double calculateVolumeV1(u8 sourceVolume);
[[nodiscard]] int calculateVolumeScalar(u8 sourceVolume);
[[nodiscard]] double calculateVolumeV2(u8 sourceVolume);

[[nodiscard]] u8 tremoloDepthToMidiValue(int sourceDepth, bool v1BgmInList);
[[nodiscard]] u8 lfoRateByteToMidiValue(u8 rate);

}  // namespace capcom_snes

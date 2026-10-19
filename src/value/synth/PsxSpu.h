/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/synth/SynthModel.h"

namespace vgmtrans::core {

inline constexpr u32 kPs1SpuSampleRate = 44100;
inline constexpr u32 kPs2SpuSampleRate = 48000;

enum class PsxSpuGeneration : u8 {
  Ps1,
  Ps2,
};

[[nodiscard]] inline constexpr u16 composePsxAdsr1(u8 attackMode, u8 attackRate, u8 decayRate, u8 sustainLevel) {
  return static_cast<u16>(((attackMode & 1) << 15) | ((attackRate & 0x7f) << 8) | ((decayRate & 0x0f) << 4) |
                          (sustainLevel & 0x0f));
}

[[nodiscard]] inline constexpr u16 composePsxAdsr2(u8 sustainMode, u8 sustainDirection, u8 sustainRate, u8 releaseMode,
                                                   u8 releaseRate) {
  return static_cast<u16>(((sustainMode & 1) << 15) | ((sustainDirection & 1) << 14) | ((sustainRate & 0x7f) << 6) |
                          ((releaseMode & 1) << 5) | (releaseRate & 0x1f));
}

// Converts the two native SPU ADSR registers into the exporter-neutral
// envelope model. PS1 and PS2 use the same registers at different sample rates.
[[nodiscard]] Envelope psxSpuEnvelope(u16 adsr1, u16 adsr2, PsxSpuGeneration generation = PsxSpuGeneration::Ps1);

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/model/EnvelopeModel.h"

#include <vector>

namespace vgmtrans::core {

inline constexpr u32 kSnesDspSampleRate = 32000;
// About one second, and one complete LFSR cycle at the fastest noise rate.
inline constexpr u32 kSnesDspNoiseSampleCount = 32767;

[[nodiscard]] constexpr u8 snesDspKonamiAdsr1(u8 encoded) {
  return encoded >= 0xa0 ? 0 : static_cast<u8>(0x80 | (((encoded % 10) & 0x07) << 4) | (encoded / 10));
}

[[nodiscard]] constexpr u8 snesDspKonamiAdsr2(u8 encoded) {
  return static_cast<u8>(((encoded / 30) << 5) | ((encoded % 30) + 2));
}

[[nodiscard]] Envelope snesDspEnvelope(u8 adsr1, u8 adsr2, u8 gain);
[[nodiscard]] double snesDspAdsrAttackSeconds(u8 attackRate);
[[nodiscard]] double snesDspAdsrDecaySeconds(u8 decayRate);
[[nodiscard]] double snesDspAdsrSustainSeconds(u8 sustainRate);
[[nodiscard]] double snesDspGainEnvelopeSeconds(u8 gain, s16 envelopeFrom, s16 envelopeTo);
// Returns the actual wall-clock time for the DSP to reach envelopeTo. Unlike
// snesDspGainEnvelopeSeconds, this does not reshape the result for a dB-linear
// synth envelope.
[[nodiscard]] double snesDspGainPhysicalSeconds(u8 gain, s16 envelopeFrom, s16 envelopeTo);
// Returns the DSP ENVX value reached after running GAIN from envelopeFrom for
// elapsedSeconds.
[[nodiscard]] s16 snesDspGainEnvelopeValue(u8 gain, s16 envelopeFrom, double elapsedSeconds);
// Synthesizes the DSP's 15-bit LFSR output. Rate is FLG bits 0-4; zero holds
// the current state instead of clocking it.
[[nodiscard]] std::vector<s16> synthesizeSnesDspNoisePcm16(u8 rate, u32 sampleCount);

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/model/EnvelopeModel.h"

namespace vgmtrans::core {

[[nodiscard]] constexpr u8 snesDspKonamiAdsr1(u8 encoded) {
  return encoded >= 0xa0 ? 0 : static_cast<u8>(0x80 | (((encoded % 10) & 0x07) << 4) | (encoded / 10));
}

[[nodiscard]] constexpr u8 snesDspKonamiAdsr2(u8 encoded) {
  return static_cast<u8>(((encoded / 30) << 5) | ((encoded % 30) + 2));
}

[[nodiscard]] Envelope snesDspEnvelope(u8 adsr1, u8 adsr2, u8 gain);
[[nodiscard]] double snesDspAdsrAttackSeconds(u8 attackRate);
[[nodiscard]] double snesDspAdsrSustainSeconds(u8 sustainRate);
[[nodiscard]] double snesDspGainEnvelopeSeconds(u8 gain, s16 envelopeFrom, s16 envelopeTo);

}  // namespace vgmtrans::core

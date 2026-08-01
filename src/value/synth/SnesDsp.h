/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/model/EnvelopeModel.h"

namespace vgmtrans::core {

[[nodiscard]] Envelope snesDspEnvelope(u8 adsr1, u8 adsr2, u8 gain);
[[nodiscard]] double snesDspAdsrAttackSeconds(u8 attackRate);
[[nodiscard]] double snesDspAdsrSustainSeconds(u8 sustainRate);
[[nodiscard]] double snesDspGainEnvelopeSeconds(u8 gain, s16 envelopeFrom, s16 envelopeTo);

}  // namespace vgmtrans::core

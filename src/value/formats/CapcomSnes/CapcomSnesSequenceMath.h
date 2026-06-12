/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "formats/CapcomSnes/CapcomSnesDriverMath.h"
#include "value/formats/CapcomSnes/CapcomSnesTypes.h"

namespace vgmtrans::formats::capcom_snes::sequence_math {

[[nodiscard]] double tuningCents(s8 tuning);
[[nodiscard]] double portamentoMillisecondsPerCent(u8 rawTime);
[[nodiscard]] u32 baseNoteTicks(u32 rawDuration);
[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u32 rawTempo);
[[nodiscard]] double volumeGain(CapcomSnesEngineVersion version, u8 rawVolume);
[[nodiscard]] ::capcom_snes::PanConversionResult panConversion(CapcomSnesEngineVersion version, u8 rawPan);
[[nodiscard]] double stereoPosition(const ::capcom_snes::PanConversionResult& converted);
[[nodiscard]] u8 tremoloDepth(CapcomSnesEngineVersion version, u8 rawDepth);
[[nodiscard]] double midi7Amount(u8 value);
[[nodiscard]] double lfoRateAmount(u8 rawRate);

}  // namespace vgmtrans::formats::capcom_snes::sequence_math

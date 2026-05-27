/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Types.h"

#include <cstdint>
#include "AkaoSnesFormat.h"
#include "Modulation.h"

namespace akao_snes::modulation {

inline constexpr u8 kDefaultTempo = 0x20;

bool isLfoActive(AkaoSnesVersion version, u8 rate, u8 depth);
u8 delayTicks(AkaoSnesVersion version, u8 delay);

VibratoModulationSpec vibratoSpec(AkaoSnesVersion version);
bool exportsTremolo(AkaoSnesVersion version);
TremoloModulationSpec tremoloSpec(AkaoSnesVersion version);

u8 vibratoDepthMidiValue(AkaoSnesVersion version, u8 rate, u8 depth);
u8 tremoloDepthMidiValue(AkaoSnesVersion version, u8 rate, u8 depth, u8 delay = 0);
u8 rateMidiValue(AkaoSnesVersion version, u8 rate, u8 depth, u8 timer0Frequency);
u8 delayMidiValue(AkaoSnesVersion version, u8 delay, u8 tempo, u8 timer0Frequency);
u32 v1VibratoRampTicks(u8 rate, u8 tempo);
u32 v3LfoRampTicks(u8 rate, u8 tempo);
u32 v4VibratoRampTicks(u8 rate, u8 tempo);

}  // namespace akao_snes::modulation

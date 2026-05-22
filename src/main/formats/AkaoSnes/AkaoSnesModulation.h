/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>
#include "AkaoSnesFormat.h"
#include "Modulation.h"

namespace akao_snes::modulation {

inline constexpr uint8_t kDefaultTempo = 0x20;
inline constexpr uint8_t kVibratoDelayController = 93;

bool supportsLfoAutomation(AkaoSnesVersion version);
bool isLfoActive(AkaoSnesVersion version, uint8_t rate, uint8_t depth);
uint8_t delayTicks(AkaoSnesVersion version, uint8_t delay);

VibratoModulationSpec vibratoSpec(AkaoSnesVersion version);

uint8_t vibratoDepthMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth);
uint8_t rateMidiValue(AkaoSnesVersion version, uint8_t rate, uint8_t depth, uint8_t timer0Frequency);
uint8_t delayMidiValue(AkaoSnesVersion version, uint8_t delay, uint8_t tempo, uint8_t timer0Frequency);
uint32_t v1VibratoRampTicks(uint8_t rate, uint8_t tempo);
uint32_t v3VibratoRampTicks(uint8_t rate, uint8_t tempo);
uint32_t v4VibratoRampTicks(uint8_t rate, uint8_t tempo);

}  // namespace akao_snes::modulation

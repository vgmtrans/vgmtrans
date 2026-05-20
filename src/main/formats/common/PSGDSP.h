#pragma once

#include <cstdint>
#include <vector>

namespace psg {

constexpr double kDefaultBaseFrequencyHz = 440.0;

std::vector<uint8_t> synthesizePulseWave(double dutyCycle, uint32_t sampleRate,
                                         uint32_t sampleCount,
                                         double baseFrequencyHz = kDefaultBaseFrequencyHz);
std::vector<uint8_t> synthesizeNoiseWave(uint32_t sampleCount, uint16_t lfsrSeed = 0x7FFF,
                                         uint16_t lfsrTap = 0x6000);

}  // namespace psg

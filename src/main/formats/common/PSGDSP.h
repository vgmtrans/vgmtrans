#pragma once

#include "base/Types.h"

#include <cstdint>
#include <vector>

namespace psg {

constexpr double kDefaultBaseFrequencyHz = 440.0;

/**
 * Synthesizes a band-limited square wave.
 * @param dutyCycle The duty cycle of the pulse wave.
 * @param sampleRate The sample rate for synthesis.
 * @param sampleCount The number of samples to generate.
 * @param baseFrequencyHz The base frequency in Hz.
 * @return A vector of bytes representing the synthesized pulse wave.
 */
std::vector<u8> synthesizeBandLimitedPulsePCM16(double dutyCycle, u32 sampleRate,
                                         u32 sampleCount,
                                         double baseFrequencyHz = kDefaultBaseFrequencyHz);
/**
 * Synthesizes a noise wave using a linear-feedback shift register algorithm.
 * @param sampleCount The number of samples to generate.
 * @param lfsrSeed The initial seed for the LFSR.
 * @param lfsrTap The tap position for the LFSR.
 * @return PCM16-encoded noise wave data.
 */
std::vector<u8> synthesizeLfsrNoisePCM16(u32 sampleCount, u16 lfsrSeed = 0x7FFF,
                                         u16 lfsrTap = 0x6000);

}  // namespace psg

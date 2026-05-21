#pragma once

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
std::vector<uint8_t> synthesizeBandLimitedPulsePCM16(double dutyCycle, uint32_t sampleRate,
                                         uint32_t sampleCount,
                                         double baseFrequencyHz = kDefaultBaseFrequencyHz);
/**
 * Synthesizes a noise wave using a linear-feedback shift register algorithm.
 * @param sampleCount The number of samples to generate.
 * @param lfsrSeed The initial seed for the LFSR.
 * @param lfsrTap The tap position for the LFSR.
 * @return PCM16-encoded noise wave data.
 */
std::vector<uint8_t> synthesizeLfsrNoisePCM16(uint32_t sampleCount, uint16_t lfsrSeed = 0x7FFF,
                                         uint16_t lfsrTap = 0x6000);

}  // namespace psg

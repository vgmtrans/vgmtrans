/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSGDSP.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>

namespace psg {

std::vector<uint8_t> synthesizeNoiseWave(uint32_t sampleCount, uint16_t lfsrSeed, uint16_t lfsrTap) {
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  if (samples.empty()) {
    return samples;
  }

  auto* output = reinterpret_cast<int16_t*>(samples.data());
  uint16_t value = lfsrSeed;
  output[0] = 0x7FFF;
  for (uint32_t i = 1; i < sampleCount; i++) {
    bool carry = (value & 0x0001) != 0;
    value >>= 1;
    if (carry) {
      output[i] = -0x7FFF;
      value ^= lfsrTap;
    } else {
      output[i] = 0x7FFF;
    }
  }

  return samples;
}

std::vector<uint8_t> synthesizePulseWave(double dutyCycle, uint32_t sampleRate,
                                         uint32_t sampleCount, double baseFrequencyHz) {
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  if (samples.empty() || sampleRate == 0 || baseFrequencyHz <= 0.0) {
    return samples;
  }

  auto* output = reinterpret_cast<int16_t*>(samples.data());

  std::vector<double> coefficients = {dutyCycle - 0.5};
  {
    int i = 1;
    const uint32_t maxHarmonics = static_cast<uint32_t>(sampleRate / (baseFrequencyHz * 2.0));
    std::generate_n(std::back_inserter(coefficients), maxHarmonics,
                    [dutyCycle, &i]() {
                      double val = sin(i * dutyCycle * M_PI) * 2 / (i * M_PI);
                      i++;
                      return val;
                    });
  }

  const double scale = baseFrequencyHz * M_PI * 2.0 / static_cast<double>(sampleRate);
  for (uint32_t i = 0; i < sampleCount; i++) {
    int counter = 0;
    double value = std::accumulate(std::begin(coefficients), std::end(coefficients), 0.0,
                                   [i, scale, &counter](double sum, double coef) {
                                     sum += coef * cos(counter++ * scale * i);
                                     return sum;
                                   });

    int16_t out_value = static_cast<int16_t>(
        std::clamp(std::round(value * 0x7FFF * 2.0), -32768.0, 32767.0));
    output[i] = out_value;
  }

  return samples;
}

}  // namespace psg

/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Types.h"
#include "PSGDSP.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>

namespace psg {

std::vector<u8> synthesizeLfsrNoisePCM16(u32 sampleCount, u16 lfsrSeed, u16 lfsrTap) {
  std::vector<u8> samples(sampleCount * sizeof(s16));
  if (samples.empty()) {
    return samples;
  }

  auto* output = reinterpret_cast<s16*>(samples.data());
  u16 value = lfsrSeed;
  output[0] = 0x7FFF;
  for (u32 i = 1; i < sampleCount; i++) {
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

std::vector<u8> synthesizeBandLimitedPulsePCM16(double dutyCycle, u32 sampleRate,
                                         u32 sampleCount, double baseFrequencyHz) {
  std::vector<u8> samples(sampleCount * sizeof(s16));
  if (samples.empty() || sampleRate == 0 || baseFrequencyHz <= 0.0) {
    return samples;
  }

  auto* output = reinterpret_cast<s16*>(samples.data());

  std::vector<double> coefficients = {dutyCycle - 0.5};
  {
    int i = 1;
    const u32 maxHarmonics = static_cast<u32>(sampleRate / (baseFrequencyHz * 2.0));
    std::generate_n(std::back_inserter(coefficients), maxHarmonics,
                    [dutyCycle, &i]() {
                      double val = sin(i * dutyCycle * M_PI) * 2 / (i * M_PI);
                      i++;
                      return val;
                    });
  }

  const double scale = baseFrequencyHz * M_PI * 2.0 / static_cast<double>(sampleRate);
  for (u32 i = 0; i < sampleCount; i++) {
    int counter = 0;
    double value = std::accumulate(std::begin(coefficients), std::end(coefficients), 0.0,
                                   [i, scale, &counter](double sum, double coef) {
                                     sum += coef * cos(counter++ * scale * i);
                                     return sum;
                                   });

    s16 out_value = static_cast<s16>(
        std::clamp(std::round(value * 0x7FFF * 2.0), -32768.0, 32767.0));
    output[i] = out_value;
  }

  return samples;
}

}  // namespace psg

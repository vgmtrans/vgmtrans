#pragma once

#include "constexprHelpers.h"
#include <array>

constexpr int K054539_CLOCK_RATE = 18432000;
#define NMI_TIMER_HERZ(data, skipCount) (((38 + (data)) * (K054539_CLOCK_RATE / 384.0f / 14400.0f)) / ((skipCount)+1))

// The volume and pan table logic is based on the MAME k054539 emulation source code by
// Olivier Galibert. That source code is distributed under the BSD-3-Clause license.

constexpr std::array<double, 256> generate_volume_table() {
  std::array<double, 256> volTable = {};
  for (int i = 0; i < 256; ++i) {
    // volTable[i] = constexpr_pow(10.0, (-36.0 * static_cast<double>(i) / 64.0) / 20.0) / 4.0;
    volTable[i] = constexpr_pow(10.0, (-36.0 * static_cast<double>(i) / 64.0) / 20.0);
  }
  return volTable;
}

constexpr std::array<double, 0xf> generate_pan_table() {
  std::array<double, 0xf> panTable = {};
  for (int i = 0; i < 0xf; ++i) {
    panTable[i] = constexpr_sqrt(static_cast<double>(i)) / constexpr_sqrt(0xe);
  }
  return panTable;
}

constexpr std::array<double, 256> volTable = generate_volume_table();
constexpr std::array<double, 0xf> panTable = generate_pan_table();

#pragma once

#include "constevalHelpers.h"
#include <array>

constexpr int K054539_CLOCK_RATE = 18'432'000;
#define NMI_TIMER_HERZ(data, skipCount) \
    (((38 + (data)) * (K054539_CLOCK_RATE/384.0/14400.0)) / ((skipCount)+1))


template <std::size_t N = 256>
consteval std::array<double, N> make_volume_table() {
  std::array<double, N> table{};
  for (std::size_t i = 0; i < N; ++i) {
    // The MAME implementation divides these values by 4, this causes them to be too quiet
    table[i] = ct::pow(10.0, (-36.0 * static_cast<double>(i) / 64.0) / 20.0);
  }
  return table;
}

template <std::size_t N = 0xF>
consteval std::array<double, N> make_pan_table() {
  std::array<double, N> table{};
  for (std::size_t i = 0; i < N; ++i) {
    table[i] = ct::sqrt(static_cast<double>(i)) / ct::sqrt(static_cast<double>(N - 1));
  }
  return table;
}

constexpr auto volTable = make_volume_table();
constexpr auto panTable = make_pan_table();
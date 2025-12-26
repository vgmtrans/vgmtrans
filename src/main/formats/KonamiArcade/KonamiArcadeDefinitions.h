#pragma once

constexpr int K054539_CLOCK_RATE = 18'432'000;
#define NMI_TIMER_HERZ(data, skipCount) \
    (((38 + (data)) * (K054539_CLOCK_RATE/384.0/14400.0)) / ((skipCount)+1))

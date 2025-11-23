#pragma once

constexpr int K053260_CLOCK_RATE = 3'579'545;
constexpr int TIM2_COUNT = 112;
constexpr int K053260_BASE_PCM_RATE = K053260_CLOCK_RATE / TIM2_COUNT;
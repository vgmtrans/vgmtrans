/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

constexpr int YM2151_CLKB = 0xF2; // remains to be seen if this varies across games
constexpr int YM2151_CLOCK_RATE = 3'579'545;
constexpr int K053260_CLOCK_RATE = 3'579'545;
constexpr int TIM2_COUNT = 112;
constexpr int K053260_BASE_PCM_RATE = K053260_CLOCK_RATE / TIM2_COUNT;
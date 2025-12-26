/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "common.h"

struct konami_xexex_instr_info {
  enum class sample_type: u8 {
    PCM_8 = 0,
    PCM_16 = 4,
    ADPCM = 8,
  };

  u8 type;
  u8 loop;
  u8 start_lo;
  u8 start_mid;
  u8 start_hi;
  u8 loop_lo;
  u8 loop_mid;
  u8 loop_hi;
  u8 attenuation;     // sets up couple other vars when >= 0x80, perhaps an env curve?
  u8 default_pan;     // when 0, pan is set to 0x18
  u8 reverb_delay_lo;
  u8 reverb_delay_hi;
  u8 reverb_vol;

  constexpr u32 start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr u32 length() const noexcept {
    return 0;
  }

  constexpr bool isAdpcm() const noexcept {
    return false;
  }

  // [[nodiscard]] constexpr sample_type type() const noexcept {
  //   return static_cast<sample_type>(type & 0b0000'1100);
  // }

  constexpr bool isReverse() const noexcept {
    return (type & 0b0010'0000) != 0;
  }
};

struct konami_xexex_drum_info {
  u8 pitch_lo;
  u8 pitch_mid;
  u8 pitch_hi;
  u8 type;
  u8 loop;
  u8 start_lo;    // also sets loop_lo
  u8 start_mid;   // also sets loop_mid
  u8 start_hi;    // also sets loop_hi
  u8 attenuation;
  u8 default_pan; // when 0, pan is set to 4 (or 0x18 when non percussion-mode? very weird)
  u8 reverb_delay_lo;
  u8 reverb_delay_hi;
  u8 reverb_vol;
  u8 unknownD;    // sets up vars when non-zero, perhaps an env curve?
  u8 unknownE;    // sets up vars when non-zero, perhaps an env curve? same as instr.atten >= 0x7F
  u8 unknownF;    // sets up vars when non-zero, perhaps an env curve?

  constexpr u32 start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr u32 length() const noexcept {
    return 0;
    // return (length_hi << 8) | length_lo;
  }

  constexpr bool isAdpcm() const noexcept {
    return false;
    // return (flags & 0b0001'0000) > 0;
  }

  constexpr bool isReverse() const noexcept {
    return (type & 0b0010'0000) != 0;
  }
};
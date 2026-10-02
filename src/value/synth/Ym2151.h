/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"

#include <array>

namespace vgmtrans::core {

// Canonical YM2151 voice data. These are chip concepts rather than register
// bytes so formats with differently packed patch tables can share one model.
enum class Ym2151LfoWaveform : u8 {
  Saw,
  Square,
  Triangle,
  Noise,
};

struct Ym2151Operator {
  u8 attackRate = 0;
  u8 firstDecayRate = 0;
  u8 secondDecayRate = 0;
  u8 releaseRate = 0;
  u8 sustainLevel = 0;
  u8 totalLevel = 0;
  u8 keyScale = 0;
  u8 multiplier = 0;
  u8 detune1 = 0;
  u8 detune2 = 0;
  bool amplitudeModulationEnabled = false;
};

struct Ym2151Lfo {
  u8 frequency = 0;
  u8 amplitudeModulationDepth = 0;
  u8 pitchModulationDepth = 0;
  Ym2151LfoWaveform waveform = Ym2151LfoWaveform::Saw;
};

struct Ym2151Voice {
  // Native YM2151 register-slot order: M1, M2, C1, C2.
  std::array<Ym2151Operator, 4> operators;
  u8 algorithm = 0;
  u8 feedback = 0;
  u8 operatorMask = 0x0f;
  u8 amplitudeModulationSensitivity = 0;
  u8 pitchModulationSensitivity = 0;
  bool leftEnabled = true;
  bool rightEnabled = true;
  bool noiseEnabled = false;
  u8 noiseFrequency = 0;
  bool lfoEnabled = false;
  bool resetLfoOnSelect = false;
  Ym2151Lfo lfo;
};

}  // namespace vgmtrans::core

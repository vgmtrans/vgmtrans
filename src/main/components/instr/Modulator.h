/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <cstdint>

enum class ModDest : uint16_t {
  None,
  // Volume Envelope (EG1)
  VolAttack,
  VolHold,
  VolDecay,
  VolSustain,
  VolRelease,
  VolDelay,
  // Modulation Envelope (EG2)
  ModAttack,
  ModHold,
  ModDecay,
  ModSustain,
  ModRelease,
  ModDelay,
  // LFOs
  VibLfoToPitch,
  VibLfoFreq,
  VibLfoDelay,
  ModLfoToPitch,
  ModLfoToFilterFc,
  ModLfoToVolume,
  ModLfoFreq,
  ModLfoDelay,
  // Modulation Envelope to Pitch/Filter
  ModEnvToPitch,
  ModEnvToFilterFc,
  // General
  Pan,
  Attenuation,
  Pitch,
  FilterCutoff,
  FilterQ,
  ReverbSend,
  ChorusSend,
};

enum class ModSourceType : uint8_t {
  None,
  CC,
  PitchBend,
  NoteOnVelocity,
  NoteOnKeyNumber,
  PolyPressure,
  ChannelPressure,
  VibratoLFO,
  ModulationLFO,
  ModulationEnvelope,
  VolumeEnvelope,
};

// SF2-compatible flags for modulator sources
enum class ModSourceFlag : uint16_t {
  None = 0,
  DirectionNegative = 0x0100,
  PolarityBipolar = 0x0200,
  TypeConcave = 0x0400,
  TypeConvex = 0x0800,
  TypeSwitch = 0x0C00
};

constexpr ModSourceFlag operator|(ModSourceFlag a, ModSourceFlag b) {
  return static_cast<ModSourceFlag>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

struct ModSource {
  ModSourceType type{ModSourceType::None};
  uint8_t index{0};                        // CC number or other index
  ModSourceFlag flags{ModSourceFlag::None};

  // Helpers for common source patterns
  static constexpr ModSource CC(uint8_t ccNum, ModSourceFlag flags = ModSourceFlag::None) {
    return {ModSourceType::CC, ccNum, flags};
  }
};

struct Modulator {
  ModSource source;
  ModDest dest;
  int16_t amount;
  ModSource sourceAmt{ModSourceType::None}; // Same as SF2
  uint16_t trans{0};  // Same as SF2 
};

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstdint>

enum class ModSource : uint8_t {
  // Keep this list to controllers expressible by both SF2 and DLS2.
  ModWheel,
  ChannelPressure,
  PolyPressure,
  PitchWheel,
  Volume,
  Pan,
  Expression,
  ReverbSend,
  ChorusSend,
};

enum class ModDest : uint8_t {
  VibLfoToPitch,
  VibLfoFreq,
  VibLfoDelay,
  ModLfoToVol,
  ModLfoFreq,
  ModLfoDelay,
  InitialAtten,
};

class ModAmount {
public:
  static ModAmount raw(int32_t amount);
  static ModAmount fromCents(double cents);
  // Absolute SF2/DLS LFO frequency units, measured as cents relative to 8.176 Hz.
  static ModAmount fromHertz(double hertz);
  // A 7-bit unipolar controller range in Hz, scaled so MIDI value 127 reaches maxHertz.
  static ModAmount fromHertzRange(double minHertz, double maxHertz);
  static ModAmount fromSeconds(double seconds);
  static ModAmount fromCentibels(double centibels);
  static ModAmount fromDecibels(double decibels);

  [[nodiscard]] bool valid() const { return m_valid; }
  [[nodiscard]] int32_t value() const { return m_value; }

private:
  constexpr ModAmount(int32_t value, bool valid) : m_value(value), m_valid(valid) {}

  int32_t m_value;
  bool m_valid;
};

struct SynthModulator {
  ModSource source;
  ModDest destination;

  // Amount units are the shared SF2/DLS semantic units for the destination:
  // cents for pitch/frequency, timecents for delay, and centibels for volume/attenuation.
  // Use ModAmount helpers at call sites when you want more readable units.
  int32_t amount;
};

struct SynthGenerator {
  ModDest destination;

  // Absolute generator amount in shared SF2/DLS semantic units:
  // cents for pitch/frequency, timecents for delay, and centibels for volume/attenuation.
  // Use ModAmount helpers at call sites when you want more readable units.
  int32_t amount;
};

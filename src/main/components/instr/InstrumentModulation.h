#pragma once

#include <cstdint>

enum class InstrumentModSource : uint8_t {
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

enum class InstrumentModDestination : uint8_t {
  VibLfoToPitch,
  VibLfoFrequency,
  VibLfoStartDelay,
  ModLfoToVolume,
  ModLfoFrequency,
  ModLfoStartDelay,
  InitialAttenuation,
};

class InstrumentParamAmount {
public:
  static InstrumentParamAmount raw(int32_t amount);
  static InstrumentParamAmount cents(double cents);

  // Absolute SF2/DLS LFO frequency units, measured as cents relative to 8.176 Hz.
  static InstrumentParamAmount hertz(double hertz);

  // A 7-bit unipolar controller range in Hz, scaled so MIDI value 127 reaches maxHertz.
  static InstrumentParamAmount hertzRange(double minHertz, double maxHertz);

  static InstrumentParamAmount seconds(double seconds);
  static InstrumentParamAmount centibels(double centibels);
  static InstrumentParamAmount attenuationDb(double decibels);

  [[nodiscard]] bool valid() const { return m_valid; }
  [[nodiscard]] int32_t value() const { return m_value; }

private:
  constexpr InstrumentParamAmount(int32_t value, bool valid) : m_value(value), m_valid(valid) {}

  int32_t m_value;
  bool m_valid;
};

struct InstrumentModulator {
  InstrumentModSource source;
  InstrumentModDestination destination;

  // Amount units are the shared SF2/DLS semantic units for the destination:
  // cents for pitch/frequency, timecents for delay, and centibels for volume/attenuation.
  // Use InstrumentParamAmount helpers at call sites when you want more readable units.
  int32_t amount;
};

struct InstrumentGenerator {
  InstrumentModDestination destination;

  // Absolute generator amount in shared SF2/DLS semantic units:
  // cents for pitch/frequency, timecents for delay, and centibels for volume/attenuation.
  // Use InstrumentParamAmount helpers at call sites when you want more readable units.
  int32_t amount;
};

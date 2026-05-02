#pragma once

#include <cstdint>

enum class InstrumentModSource : uint8_t {
  // Keep this list to controllers expressible by both SF2 and DLS2.
  ModWheel = 1,
  Volume = 7,
  Pan = 10,
  Expression = 11,
};

enum class InstrumentModDestination : uint8_t {
  VibLfoToPitch,
  VibLfoFrequency,
  VibLfoStartDelay,
  ModLfoToVolume,
  ModLfoFrequency,
  ModLfoStartDelay,
};

struct InstrumentModulator {
  InstrumentModSource source;
  InstrumentModDestination destination;

  // Amount units are the shared SF2/DLS semantic units for the destination:
  // cents for pitch/frequency, timecents for delay, and centibels for volume.
  int32_t amount;
};

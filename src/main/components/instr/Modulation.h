/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

struct DelayRange {
  double minSeconds;
  double maxSeconds;
};

struct VibratoModulationSpec {
  double maxDepthCents;
  double minHertz;
  double maxHertz;
  std::optional<DelayRange> delayRange = std::nullopt;
};

enum class TremoloGainMode {
  // SF2 modLfoToVolume is centered around nominal gain, so tremolo can boost above normal volume.
  BipolarAroundNominal,
  // Adds matching initial attenuation; the loudest tremolo point is nominal gain.
  NoBoost,
};

struct TremoloModulationSpec {
  double maxDepthDb;
  double minHertz;
  double maxHertz;
  TremoloGainMode gainMode = TremoloGainMode::BipolarAroundNominal;
  std::optional<DelayRange> delayRange = std::nullopt;
};

enum class ModSource : s16 {
  None = -1,

  // Values 0..127 represent MIDI continuous controllers. SF2 can use any CC;
  // DLS2 only accepts a strict subset.
  ModWheel = 1,
  Breath = 2,
  Volume = 7,
  Pan = 10,
  Expression = 11,
  SoundController6 = 75,
  VibratoRate = 76,
  VibratoDepth = 77,
  VibratoDelay = 78,
  SoundController10 = 79,
  ReverbSend = 91,
  TremoloDepth = 92,
  ChorusSend = 93,

  ChannelPressure = 128,
  PolyPressure = 129,
  PitchWheel = 130,
};

enum class ModDest : u8 {
  VibLfoToPitch,
  VibLfoFreq,
  VibLfoDelay,
  ModLfoToVol,
  ModLfoFreq,
  ModLfoDelay,
  InitialAtten,
};

inline constexpr std::array<ModDest, 7> kModDests {
    ModDest::VibLfoToPitch,
    ModDest::VibLfoFreq,
    ModDest::VibLfoDelay,
    ModDest::ModLfoToVol,
    ModDest::ModLfoFreq,
    ModDest::ModLfoDelay,
    ModDest::InitialAtten,
};

constexpr size_t modDestIndex(ModDest destination) {
  return static_cast<size_t>(destination);
}

constexpr ModSource modSourceForMidiController(u8 controller) {
  return static_cast<ModSource>(controller);
}

constexpr bool isMidiControllerModSource(ModSource source) {
  const int sourceValue = static_cast<int>(source);
  return sourceValue >= 0 && sourceValue <= 127;
}

constexpr std::optional<u8> midiControllerForModSource(ModSource source) {
  return isMidiControllerModSource(source)
      ? std::optional<u8>(static_cast<u8>(source))
      : std::nullopt;
}

class ModAmount {
public:
  static ModAmount raw(s32 amount);
  static ModAmount fromCents(double cents);
  // Absolute SF2/DLS LFO frequency units, measured as cents relative to 8.176 Hz.
  static ModAmount fromHertz(double hertz);
  // A 7-bit unipolar controller range in Hz, scaled so MIDI value 127 reaches maxHertz.
  static ModAmount fromHertzRange(double minHertz, double maxHertz);
  static ModAmount fromSeconds(double seconds);
  // A 7-bit unipolar controller range in timecents, scaled so MIDI value 127 reaches maxSeconds.
  // minSeconds is clamped to the smallest normal SF2 delay because the zero-delay sentinel
  // cannot anchor a continuous range.
  static ModAmount fromSecondsRange(double minSeconds, double maxSeconds);
  static ModAmount fromCentibels(double centibels);
  static ModAmount fromDecibels(double decibels);

  [[nodiscard]] bool valid() const { return m_valid; }
  [[nodiscard]] s32 value() const { return m_value; }

private:
  constexpr ModAmount(s32 value, bool valid) : m_value(value), m_valid(valid) {}

  s32 m_value;
  bool m_valid;
};

// Convert an absolute frequency into the 7-bit MIDI value that drives a
// ParamAmount::hertzRange(minHertz, maxHertz) modulator or generator.
u8 midiValueForHertzInRange(double hertz, double minHertz, double maxHertz);
double clampSecondsRangeMinimum(double seconds);
u8 midiValueForSecondsInRange(double seconds, double minSeconds, double maxSeconds);

struct SynthModulator {
  SynthModulator(ModSource explicitSource, ModDest destination, s32 amount)
      : source(explicitSource),
        destination(destination),
        amount(amount) {}

  SynthModulator(ModDest destination, s32 amount)
      : source(std::nullopt),
        destination(destination),
        amount(amount) {}

  // If source is empty, exporters resolve it from settings using destination.
  std::optional<ModSource> source;
  ModDest destination;

  // Amount units are the shared SF2/DLS semantic units for the destination:
  // cents for pitch/frequency, timecents for delay, and centibels for volume/attenuation.
  // Use ModAmount helpers at call sites when you want more readable units.
  s32 amount;
};

struct SynthGenerator {
  ModDest destination;

  // Absolute generator amount in shared SF2/DLS semantic units:
  // cents for pitch/frequency, timecents for delay, and centibels for volume/attenuation.
  // Use ModAmount helpers at call sites when you want more readable units.
  s32 amount;
};

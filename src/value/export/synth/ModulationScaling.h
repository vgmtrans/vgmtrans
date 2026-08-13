/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/midi/MidiModel.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/synth/SynthModel.h"
#include "value/export/ExportPolicy.h"

#include <optional>
#include <vector>

namespace vgmtrans::core {

// Export-lowering vocabulary. These records are deliberately outside the
// synth model: format authors describe physical modulation there, and only
// exporters need routing destinations and controller sources.
enum class SynthDestination {
  Pitch,
  FilterCutoff,
  VolumeAttenuation,
  Pan,
  VibratoDepth,
  VibratoRate,
  VibratoDelay,
  TremoloDepth,
  TremoloRate,
  TremoloDelay,
  Unknown,
};

enum class SynthSource {
  NoteOnVelocity,
  KeyNumber,
  Lfo,
  Envelope,
  MidiController,
  ChannelPressure,
  PolyPressure,
  PitchWheel,
  Unknown,
};

struct SynthGenerator {
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;

  friend bool operator==(const SynthGenerator&, const SynthGenerator&) = default;
};

struct SynthModulator {
  std::optional<SynthSource> source;
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;

  friend bool operator==(const SynthModulator&, const SynthModulator&) = default;
};

struct LoweredSynthModulation {
  std::vector<SynthGenerator> generators;
  std::vector<SynthModulator> modulators;
};

// Translate physical instrument modulation once before an exporter writes its
// target-specific records.
[[nodiscard]] LoweredSynthModulation lowerSynthModulation(const InstrumentModulation& modulation);

// Helpers used when the user wants vibrato/tremolo controls scaled to the values
// observed in the sequence instead of the full possible 0-127 range.
[[nodiscard]] u8 scaledMidiModulationControllerValue(u8 value, const ObservedValueRange* range,
                                                     ModulationScalingPolicy policy) noexcept;

void applyMidiModulationScaling(MidiSequence& sequence, const MidiModulationUsage& usage,
                                ModulationScalingPolicy policy);

[[nodiscard]] s32 scaledSynthModulatorAmount(const SynthModulator& modulator, const MidiModulationUsage* usage,
                                             ModulationScalingPolicy policy) noexcept;

[[nodiscard]] bool shouldExportSynthGenerator(const SynthGenerator& generator,
                                              ModulationConversionPolicy conversion) noexcept;

[[nodiscard]] bool shouldExportSynthModulator(const SynthModulator& modulator,
                                              ModulationConversionPolicy conversion) noexcept;

}  // namespace vgmtrans::core

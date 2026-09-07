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
// exporters need routing destinations and controller sources. Keep this limited
// to the routes produced by physical modulation lowering.
enum class SynthDestination {
  VolumeAttenuation,
  VibratoDepth,
  VibratoRate,
  VibratoDelay,
  TremoloDepth,
  TremoloRate,
  TremoloDelay,
  Unknown,
};

enum class SynthSource {
  DefaultController,
  ChannelPressure,
};

struct SynthGenerator {
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;

  friend bool operator==(const SynthGenerator&, const SynthGenerator&) = default;
};

struct SynthModulator {
  SynthSource source = SynthSource::DefaultController;
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;

  friend bool operator==(const SynthModulator&, const SynthModulator&) = default;
};

struct LoweredSynthModulation {
  std::vector<SynthGenerator> generators;
  std::vector<SynthModulator> modulators;
};

// SoundFont/DLS controller units used by modulation lowering and MIDI
// normalization. Source formats retain physical Hz/seconds instead.
[[nodiscard]] s32 synthAmountFromHertz(double hertz);
[[nodiscard]] s32 synthAmountFromHertzRange(double minHertz, double maxHertz);
[[nodiscard]] s32 synthAmountFromSeconds(double seconds);
[[nodiscard]] s32 synthAmountFromCentibels(double centibels);
[[nodiscard]] s32 synthAmountFromDecibels(double decibels);
[[nodiscard]] double synthSecondsRangeMinimum(double seconds);

// Translate physical instrument modulation once before an exporter writes its
// target-specific records.
[[nodiscard]] LoweredSynthModulation lowerSynthModulation(const InstrumentModulation& modulation);

// Helpers used when the user wants vibrato/tremolo controls scaled to the values
// observed in the sequence instead of the full possible 0-127 range.
[[nodiscard]] u8 scaledMidiModulationControllerValue(u8 value, const MidiModulationMaximum* maximum,
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

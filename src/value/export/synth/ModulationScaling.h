/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/midi/ModulationAnalysis.h"
#include "value/synth/SynthModel.h"
#include "value/export/ExportPolicy.h"

namespace vgmtrans::core {

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

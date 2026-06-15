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

// Helpers used when the user wants vibrato/tremolo controls scaled to the values
// observed in the sequence instead of the full possible 0-127 range.
[[nodiscard]] u8 scaledMidiModulationControllerValue(u8 value, const ObservedValueRange* range,
                                                     ModulationScalingPolicy policy) noexcept;

void applyMidiModulationScaling(MidiSequence& sequence, const MidiModulationUsage& usage,
                                ModulationScalingPolicy policy);

[[nodiscard]] s32 scaledSynthModulatorAmount(const SynthModulator& modulator, const MidiModulationUsage* usage,
                                             ModulationScalingPolicy policy) noexcept;

}  // namespace vgmtrans::core

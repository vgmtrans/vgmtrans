/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/midi/MidiModel.h"
#include "value/sequence/PerformanceModel.h"
#include "value/export/ExportTypes.h"
#include "value/synth/SynthModel.h"

#include <span>

namespace vgmtrans::core {

// Converts SequenceVm output into MIDI events. This is where parsed performance
// values become MIDI channels, ports, controller numbers, and quantized controller values.
[[nodiscard]] MidiSequence renderMidiSequence(
    const PerformanceSequence& performance, MidiExportOptions options = {},
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators,
    std::span<const InstrumentSetAsset* const> instrumentSets = {});

}  // namespace vgmtrans::core

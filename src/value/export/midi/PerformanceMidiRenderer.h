/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/midi/MidiModel.h"
#include "value/export/ResolvedPerformance.h"
#include "value/export/ExportTypes.h"
#include "value/synth/SynthModel.h"

#include <span>

namespace vgmtrans::core {

struct SequenceModulationProfile;

[[nodiscard]] MidiSequence renderMidiSequence(
    const ResolvedPerformance& performance, const InstrumentAddressPlan& layout, MidiExportOptions options = {},
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators,
    const SequenceModulationProfile* modulationProfile = nullptr);

}  // namespace vgmtrans::core

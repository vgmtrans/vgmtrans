/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/base/Source.h"
#include "value/synth/SynthModel.h"
#include "value/export/ExportPolicy.h"

#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct MidiModulationUsage;

struct SoundFontInput {
  std::string name;
  std::span<const InstrumentSetAsset* const> instrumentSets;
  std::span<const SampleCollectionAsset* const> sampleCollections;
  const MidiModulationUsage* midiModulationUsage = nullptr;
  ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange;
  ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators;
};

struct SoundFontResult {
  std::vector<u8> bytes;
  std::vector<Diagnostic> diagnostics;
};

// Writes resolved instrument/sample assets as an SF2 container. Sample bytes are
// decoded through SourceStore so source-backed diagnostics remain available.
class SoundFontExporter {
public:
  [[nodiscard]] SoundFontResult exportSoundFont(const SoundFontInput& input, const SourceStore& sources) const;
};

}  // namespace vgmtrans::core

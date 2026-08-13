/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/base/Source.h"
#include "value/synth/SynthModel.h"
#include "value/export/ExportPolicy.h"

#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct MidiModulationUsage;

struct DlsInput {
  std::string name;
  std::span<const InstrumentSetAsset* const> instrumentSets;
  std::span<const SampleCollectionAsset* const> sampleCollections;
  const MidiModulationUsage* midiModulationUsage = nullptr;
  ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange;
};

struct DlsResult {
  std::vector<u8> bytes;
  std::vector<Diagnostic> diagnostics;
};

// Writes resolved instrument/sample assets as a DLS container. Sample bytes are
// decoded through SourceStore so source-backed diagnostics remain available.
class DlsExporter {
public:
  [[nodiscard]] DlsResult exportDls(const DlsInput& input, const SourceStore& sources) const;
};

}  // namespace vgmtrans::core

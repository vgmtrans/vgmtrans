/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/core/Model.h"
#include "value/core/SampleDecoder.h"
#include "value/core/Source.h"

#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct ModulationUsage;

struct SoundFontInput {
  std::string name;
  std::span<const InstrumentSetAsset* const> instrumentSets;
  std::span<const SampleCollectionAsset* const> sampleCollections;
  const ModulationUsage* modulationUsage = nullptr;
};

struct SoundFontResult {
  std::vector<u8> bytes;
  std::vector<Diagnostic> diagnostics;
};

class SoundFontExporter {
public:
  [[nodiscard]] SoundFontResult exportSoundFont(const SoundFontInput& input, const SourceStore& sources) const;
};

}  // namespace vgmtrans::core

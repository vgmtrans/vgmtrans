/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "core/Model.h"
#include "core/SampleDecoder.h"
#include "core/Source.h"

#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct SoundFontInput {
  std::string name;
  std::span<const InstrumentBankAsset* const> instrumentBanks;
  std::span<const SampleCollectionAsset* const> sampleCollections;
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

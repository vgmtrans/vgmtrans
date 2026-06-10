/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "core/Model.h"
#include "core/Source.h"

#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct DlsInput {
  std::string name;
  std::span<const InstrumentSetAsset* const> instrumentSets;
  std::span<const SampleCollectionAsset* const> sampleCollections;
};

struct DlsResult {
  std::vector<u8> bytes;
  std::vector<Diagnostic> diagnostics;
};

class DlsExporter {
public:
  [[nodiscard]] DlsResult exportDls(const DlsInput& input, const SourceStore& sources) const;
};

}  // namespace vgmtrans::core

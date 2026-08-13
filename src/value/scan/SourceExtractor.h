/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"

#include <functional>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct ExtractionInput {
  SourceFile source;
  ByteReader reader;
};

struct ExtractionResult {
  std::vector<ExtractedSource> sources;
  std::vector<Diagnostic> diagnostics;
};

// Extractors recognize containers and replace them with ordinary derived
// sources. Producing at least one valid child consumes the input; an empty
// result leaves it available to later extractors and format modules.
struct SourceExtractor {
  using Extract = std::function<ExtractionResult(const ExtractionInput& input)>;

  std::string name;
  std::vector<std::string> acceptedFormats;
  Extract extract;
};

}  // namespace vgmtrans::core

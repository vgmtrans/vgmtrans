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
  using ResolvePath = std::function<std::optional<SourceFile>(const std::filesystem::path& path)>;

  std::string name;
  std::vector<std::string> acceptedFormats;
  Extract extract;
  // Optional hook for filesystem loads, before reading bytes. Return metadata
  // with the path to load, or nullopt to decline. The first match in registration
  // order wins; its path is not resolved again. An empty name defaults to the filename.
  ResolvePath resolvePath;
};

}  // namespace vgmtrans::core

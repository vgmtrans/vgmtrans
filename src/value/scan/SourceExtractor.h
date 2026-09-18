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
  // Read-only access to sibling archive members during extraction.
  const SourceStore& sources;
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
  using FileExists = std::function<bool(const std::filesystem::path&)>;
  using ResolvePath = std::function<std::optional<SourceFile>(const std::filesystem::path& path,
                                                            const FileExists& exists)>;

  std::string name;
  std::vector<std::string> acceptedFormats;
  Extract extract;
  // Optional hook for disk files and archive members. Paths and exists use the
  // same namespace. Return a path and metadata, or nullopt to decline. The first
  // match wins. Disk files resolve before I/O; archive redirects reuse a member
  // source, which is scanned at most once.
  ResolvePath resolvePath;
};

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/FormatRegistry.h"
#include "value/core/ProjectModel.h"
#include "value/core/SequenceDialect.h"
#include "value/core/Source.h"
#include "value/export/ExportTypes.h"

#include <filesystem>
#include <vector>

namespace vgmtrans::core {

// Session is the stateful facade around the value-oriented pipeline. The
// scanning/export algorithms remain plain functions, while Session owns the
// current source store, registries, and latest immutable project snapshot.
class Session {
public:
  SourceId addSource(SourceFile file, std::vector<u8> bytes);
  SourceId addSourceFromPath(std::filesystem::path path);

  // Rebuild the project from all current sources and registered formats.
  // Extractors may append virtual sources, so repeated scans intentionally
  // recreate the project rather than mutating previous assets in place.
  [[nodiscard]] Project scan();

  // Export helpers are thin convenience wrappers over value/export/Export.cpp.
  // The ExportRequest selects containers and policies; it does not alter the
  // stored parsed project.
  [[nodiscard]] std::vector<Artifact> exportCollection(CollectionId id, const ExportRequest& request) const;

  [[nodiscard]] std::vector<CollectionExport> exportAllCollections(const ExportRequest& request) const;

  [[nodiscard]] const SourceStore& sources() const noexcept { return sources_; }
  [[nodiscard]] SourceStore& sources() noexcept { return sources_; }
  [[nodiscard]] const FormatRegistry& formats() const noexcept { return formats_; }
  [[nodiscard]] FormatRegistry& formats() noexcept { return formats_; }
  [[nodiscard]] const SequenceDialectRegistry& dialects() const noexcept { return dialects_; }
  [[nodiscard]] SequenceDialectRegistry& dialects() noexcept { return dialects_; }
  [[nodiscard]] const Project& project() const noexcept { return project_; }

private:
  SourceStore sources_;
  FormatRegistry formats_;
  SequenceDialectRegistry dialects_;
  Project project_;
};

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/export/ExportTypes.h"
#include "value/model/SessionSnapshot.h"
#include "value/scan/FormatRegistry.h"
#include "value/scan/ScanTypes.h"
#include "value/sequence/SequenceDialect.h"

#include <filesystem>
#include <set>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Session is the mutable owner of the value pipeline. It owns source bytes,
// discovered assets, match facts, collections, diagnostics, and registries.
// Call snapshot() when UI, tests, or export need a stable read model.
class Session {
public:
  SourceId addSource(SourceFile file, std::vector<u8> bytes);
  SourceId addSourceFromPath(std::filesystem::path path);

  [[nodiscard]] SessionSnapshot scanSource(SourceId id);
  [[nodiscard]] SessionSnapshot scanPendingSources();
  [[nodiscard]] SessionSnapshot snapshot() const;

  [[nodiscard]] std::vector<Artifact> exportCollection(CollectionId id, const ExportRequest& request) const;
  [[nodiscard]] std::vector<CollectionExport> exportAllCollections(const ExportRequest& request) const;

  [[nodiscard]] const SourceStore& sources() const noexcept { return sources_; }
  [[nodiscard]] SourceStore& sources() noexcept { return sources_; }
  [[nodiscard]] const FormatRegistry& formats() const noexcept { return formats_; }
  [[nodiscard]] FormatRegistry& formats() noexcept { return formats_; }
  [[nodiscard]] const SequenceDialectRegistry& dialects() const noexcept { return dialects_; }
  [[nodiscard]] SequenceDialectRegistry& dialects() noexcept { return dialects_; }

private:
  void scanSourceAndDerived(SourceId id, u32 loadGroup);
  void scanOneSource(SourceId id, u32 loadGroup, std::vector<SourceId>& queue, std::set<u32>& queued);
  void rebuildCollections();
  void reconcileCollections(std::vector<DesiredCollection> desiredCollections);

  SourceStore sources_;
  std::vector<Asset> assets_;
  std::vector<MatchFact> matchFacts_;
  std::vector<Collection> collections_;
  std::vector<Diagnostic> diagnostics_;
  FormatRegistry formats_;
  SequenceDialectRegistry dialects_;
  ScanIdAllocator ids_;
  std::unordered_set<u32> scannedSources_;
  u32 nextLoadGroup_ = 1;
};

}  // namespace vgmtrans::core

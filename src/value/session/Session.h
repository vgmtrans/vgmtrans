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
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Session is the mutable state for one loaded workspace. It owns the source
// bytes, the assets found inside them, the facts used to match related assets,
// the collections built from those matches, and the format registries.
// Call snapshot() when UI, tests, or export need a stable read-only view.
class Session {
public:
  SourceId addSource(SourceFile file, std::vector<u8> bytes);
  SourceId addSourceFromPath(std::filesystem::path path);
  [[nodiscard]] SessionSnapshot removeSource(SourceId id);

  [[nodiscard]] SessionSnapshot scanSource(SourceId id);
  [[nodiscard]] SessionSnapshot scanPendingSources();
  [[nodiscard]] SessionSnapshot snapshot() const;

  [[nodiscard]] std::vector<Artifact> exportCollection(CollectionId id, const ExportRequest& request) const;
  [[nodiscard]] std::vector<CollectionExport> exportAllCollections(const ExportRequest& request) const;

  [[nodiscard]] const SourceStore& sources() const noexcept { return sources_; }
  [[nodiscard]] const FormatRegistry& formats() const noexcept { return formats_; }
  [[nodiscard]] FormatRegistry& formats() noexcept { return formats_; }
  [[nodiscard]] const SequenceDialectRegistry& dialects() const noexcept { return dialects_; }
  [[nodiscard]] SequenceDialectRegistry& dialects() noexcept { return dialects_; }

private:
  void scanSourceAndDerived(SourceId id);
  void scanOneSource(SourceId id, std::vector<SourceId>& queue, std::set<u32>& queued);
  void appendScanAssets(std::vector<Asset> assets, SourceId owner);
  void removeDiscoveredDataForSources(const std::vector<SourceId>& sources);
  void rebuildCollections();
  void reconcileCollections(std::string_view resolverId, std::vector<DesiredCollection> desiredCollections);
  void markCollectionsStaleForAssets(const std::unordered_set<u32>& assetIds);

  SourceStore sources_;
  std::vector<Asset> assets_;
  std::vector<MatchFact> matchFacts_;
  std::vector<Collection> collections_;
  std::vector<Diagnostic> diagnostics_;
  std::unordered_map<u32, u32> assetSourceOwners_;
  FormatRegistry formats_;
  SequenceDialectRegistry dialects_;
  ScanIdAllocator ids_;
  std::unordered_set<u32> scannedSources_;
};

}  // namespace vgmtrans::core

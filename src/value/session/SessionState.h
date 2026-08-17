/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Private mutable state for a Session. Large scan-owned sequences retain
// immutable per-scan backing so snapshots can share them across revisions.
// Session-only bookkeeping remains ordinary mutable state.
class SessionState {
public:
  [[nodiscard]] const SharedSequence<Asset>& assets() const noexcept { return assets_; }
  [[nodiscard]] const SharedSequence<MatchFact>& matchFacts() const noexcept { return matchFacts_; }
  [[nodiscard]] const std::vector<Collection>& collections() const noexcept { return collections_; }
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }

  [[nodiscard]] bool containsAsset(AssetId id) const noexcept;
  [[nodiscard]] const Asset* asset(AssetId id) const noexcept;

  template <typename T>
  [[nodiscard]] const T* asset(AssetId id) const noexcept {
    const auto* found = asset(id);
    return found != nullptr ? std::get_if<T>(found) : nullptr;
  }

  void appendScan(SourceId origin, ScanResult result);

  [[nodiscard]] bool removeAssets(std::span<const AssetId> assets);
  void removeSources(std::span<const SourceId> sources);
  [[nodiscard]] CollectionId createUserCollection(std::string name, CollectionMembers members, CollectionBinder binder,
                                                  ScanIdAllocator& ids);

  void addError(std::string message, std::optional<SourceRange> range = std::nullopt);
  void addDiagnostics(std::vector<Diagnostic> diagnostics);

  [[nodiscard]] const SourceMap& sourceMap() const noexcept { return sourceMap_; }
  [[nodiscard]] SourceMap sourceMapForAsset(AssetId asset) const;
  [[nodiscard]] std::map<std::string, std::vector<DesiredCollection>> desiredCollectionsByResolver() const;
  void reconcileCollections(std::string_view resolver, std::vector<DesiredCollection> desired, CollectionBinder binder,
                            ScanIdAllocator& ids);

private:
  struct ScanChunk {
    std::shared_ptr<const std::vector<Asset>> assets;
    std::shared_ptr<const std::vector<MatchFact>> matchFacts;
    SourceMap sourceMap;

    [[nodiscard]] bool empty() const noexcept { return assets->empty() && matchFacts->empty() && sourceMap.empty(); }
  };

  struct ExplicitCollectionEntry {
    SourceId origin;
    ExplicitCollection collection;
  };

  void removeDiscoveredData(const std::unordered_set<u32>& sourceIds, const std::unordered_set<u32>& assetIds);
  void updateCollectionsForRemovedAssets(const std::unordered_set<u32>& assetIds);
  void validateCollectionAssetReferences(std::string_view resolver, DesiredCollection& desired);
  [[nodiscard]] CollectionId nextCollectionId(ScanIdAllocator& ids) const;
  void rebuildViews();
  void rebuildIndexes();

  std::vector<ScanChunk> scanChunks_;
  SharedSequence<Asset> assets_;
  SharedSequence<MatchFact> matchFacts_;
  SourceMap sourceMap_;
  std::vector<ExplicitCollectionEntry> explicitCollections_;
  std::vector<Collection> collections_;
  std::vector<Diagnostic> diagnostics_;

  std::unordered_map<u32, const Asset*> assetsById_;
};

}  // namespace vgmtrans::core

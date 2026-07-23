/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Private mutable state for a Session. ScanResult is the staging value; once
// validated, all durable values are published here together. Keeping mutation in
// one place makes removal and cross-reference cleanup one state transition.
class SessionState {
public:
  [[nodiscard]] const std::vector<Asset>& assets() const noexcept { return assets_; }
  [[nodiscard]] const std::vector<MatchFact>& matchFacts() const noexcept { return matchFacts_; }
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

  void addError(std::string message, std::optional<SourceRange> range = std::nullopt);
  void addDiagnostics(std::vector<Diagnostic> diagnostics);

  [[nodiscard]] SourceMap sourceMap() const;
  [[nodiscard]] SourceMap sourceMapForAsset(AssetId asset) const;
  [[nodiscard]] std::map<std::string, std::vector<DesiredCollection>> desiredCollectionsByResolver() const;
  void reconcileCollections(std::string_view resolver, std::vector<DesiredCollection> desired, ScanIdAllocator& ids);

private:
  struct ExplicitCollectionEntry {
    SourceId origin;
    ExplicitCollection collection;
  };

  void removeDiscoveredData(const std::unordered_set<u32>& sourceIds, const std::unordered_set<u32>& assetIds);
  void markCollectionsStaleForAssets(const std::unordered_set<u32>& assetIds);
  void validateCollectionAssetReferences(std::string_view resolver, DesiredCollection& desired);
  [[nodiscard]] CollectionId nextCollectionId(ScanIdAllocator& ids) const;
  [[nodiscard]] std::optional<AssetId> annotationAssetOwner(SourceAnnotationId id) const;
  void rebuildIndexes();

  std::vector<Asset> assets_;
  std::vector<MatchFact> matchFacts_;
  std::vector<ExplicitCollectionEntry> explicitCollections_;
  std::vector<SourceAnnotation> annotations_;
  std::vector<Collection> collections_;
  std::vector<Diagnostic> diagnostics_;

  std::unordered_map<u32, size_t> assetsById_;
  std::unordered_map<u32, size_t> annotationsById_;
};

}  // namespace vgmtrans::core

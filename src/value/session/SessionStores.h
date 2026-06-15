/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

struct AssetUpsertResult {
  std::unordered_map<u32, AssetId> remappedIds;
};

class AssetStore {
public:
  void clear();
  [[nodiscard]] AssetUpsertResult upsertDiscovered(std::vector<Asset> assets);
  [[nodiscard]] std::unordered_set<u32> removeForSources(const std::unordered_set<u32>& sourceIds,
                                                         const std::unordered_set<u32>& additionalAssetIds);
  [[nodiscard]] std::vector<Asset> snapshot() const { return assets_; }

private:
  void rebuildIndexes();

  std::vector<Asset> assets_;
  std::unordered_map<std::string, size_t> assetsByStableKey_;
  std::unordered_map<std::string, AssetId> retiredIdsByStableKey_;
  std::unordered_map<u32, size_t> assetsById_;
};

class MatchFactStore {
public:
  void clear();
  void add(std::vector<MatchFact> facts, const std::unordered_map<u32, AssetId>& remappedIds);
  [[nodiscard]] std::unordered_set<u32> assetIdsForSources(const std::unordered_set<u32>& sourceIds) const;
  void removeForSourcesOrAssets(const std::unordered_set<u32>& sourceIds, const std::unordered_set<u32>& assetIds);
  [[nodiscard]] std::vector<MatchFact> snapshot() const { return facts_; }

private:
  std::vector<MatchFact> facts_;
};

class CollectionStore {
public:
  void clear();
  void reconcile(std::vector<DesiredCollection> desiredCollections, const std::set<std::string>& activeResolvers,
                 ScanIdAllocator& ids);
  void markReferencesStale(const std::unordered_set<u32>& assetIds);
  [[nodiscard]] std::vector<Collection> snapshot() const { return collections_; }

private:
  void rebuildIndex();

  std::vector<Collection> collections_;
  std::unordered_map<std::string, size_t> collectionsByKey_;
};

class DiagnosticStore {
public:
  void clear();
  void add(std::vector<Diagnostic> diagnostics);
  void add(Diagnostic diagnostic);
  void removeForSources(const std::unordered_set<u32>& sourceIds);
  [[nodiscard]] std::vector<Diagnostic> snapshot() const { return diagnostics_; }

private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace vgmtrans::core

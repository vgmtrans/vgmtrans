/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/Session.h"

#include "value/export/Export.h"
#include "value/scan/FormatModule.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] Diagnostic sessionError(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
      .range = range,
  };
}

void addMissingSequenceDialectDiagnostics(SessionSnapshot& snapshot, const SequenceDialectRegistry& dialects) {
  for (const auto& asset : snapshot.assets) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || dialects.contains(sequence->program.dialect.value)) {
      continue;
    }

    snapshot.diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .message = "No sequence dialect registered for '" + sequence->program.dialect.value + "'",
        .range = sequence->metadata.range.valid() ? std::optional<SourceRange>{sequence->metadata.range} : std::nullopt,
    });
  }
}

[[nodiscard]] std::string collectionKeyString(const CollectionKey& key) {
  return key.resolver + '\x1f' + key.value;
}

[[nodiscard]] std::unordered_set<u32> sourceIdSet(const std::vector<SourceId>& sources) {
  std::unordered_set<u32> ids;
  ids.reserve(sources.size());
  for (const SourceId source : sources) {
    ids.insert(source.value);
  }
  return ids;
}

[[nodiscard]] bool diagnosticFromSource(const Diagnostic& diagnostic, const std::unordered_set<u32>& sourceIds) {
  return diagnostic.range && sourceIds.contains(diagnostic.range->source.value);
}

[[nodiscard]] bool factFromSource(const MatchFact& fact, const std::unordered_set<u32>& sourceIds) {
  return fact.scope.source && sourceIds.contains(fact.scope.source->value);
}

[[nodiscard]] bool referencesAnyAsset(const Collection& collection, const std::unordered_set<u32>& assetIds) {
  const auto referencesOne = [&](std::optional<AssetId> id) { return id && assetIds.contains(id->value); };
  const auto referencesMany = [&](const std::vector<AssetId>& ids) {
    return std::ranges::any_of(ids, [&](AssetId id) { return assetIds.contains(id.value); });
  };
  return referencesOne(collection.sequence) || referencesMany(collection.instrumentSets) ||
         referencesMany(collection.sampleCollections) || referencesMany(collection.miscAssets);
}

[[nodiscard]] bool hasIssueCode(const Collection& collection, std::string_view code) {
  return std::ranges::any_of(collection.issues, [code](const CollectionIssue& issue) { return issue.code == code; });
}

[[nodiscard]] CollectionStatus statusWithIssues(const DesiredCollection& collection) {
  if (collection.status != CollectionStatus::Complete) {
    return collection.status;
  }
  const bool hasError = std::ranges::any_of(
      collection.issues, [](const CollectionIssue& issue) { return issue.severity == Severity::Error; });
  return hasError ? CollectionStatus::Incomplete : CollectionStatus::Complete;
}

}  // namespace

SourceId Session::addSource(SourceFile file, std::vector<u8> bytes) {
  sealRegistries();
  file.kind = SourceKind::UserLoaded;
  return sources_.add(std::move(file), std::move(bytes));
}

SourceId Session::addSourceFromPath(std::filesystem::path path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("failed to open source file: " + path.string());
  }

  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0) {
    throw std::runtime_error("failed to stat source file: " + path.string());
  }
  file.seekg(0, std::ios::beg);

  std::vector<u8> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!file) {
    throw std::runtime_error("failed to read source file: " + path.string());
  }

  return addSource(
      SourceFile{
          .name = path.filename().string(),
          .path = std::move(path),
      },
      std::move(bytes));
}

SessionSnapshot Session::removeSource(SourceId id) {
  sealRegistries();
  if (!sources_.hasSlot(id)) {
    throw std::out_of_range("Cannot remove a SourceId that is not present in the Session");
  }

  if (!sources_.contains(id)) {
    return snapshot();
  }

  const auto removedSources = sources_.removeFamily(id);
  for (const SourceId source : removedSources) {
    scannedSources_.erase(source.value);
  }

  removeDiscoveredDataForSources(removedSources);
  rebuildCollections();
  return snapshot();
}

// Scan this source if it has not been scanned yet. Any files extracted from it are
// added as derived sources and scanned before this call returns.
SessionSnapshot Session::scanSource(SourceId id) {
  sealRegistries();
  if (!sources_.contains(id)) {
    throw std::out_of_range("Cannot scan a SourceId that is not present in the Session");
  }

  if (scannedSources_.contains(id.value)) {
    return snapshot();
  }

  scanSourceAndDerived(id);
  rebuildCollections();
  return snapshot();
}

// Scan every user-loaded source that is still pending. Derived sources are skipped
// here because scanning their parent source already scans them.
SessionSnapshot Session::scanPendingSources() {
  sealRegistries();
  bool scannedAny = false;
  for (const SourceId source : sources_.activeUserSources()) {
    if (scannedSources_.contains(source.value)) {
      continue;
    }

    scanSourceAndDerived(source);
    scannedAny = true;
  }

  if (scannedAny) {
    rebuildCollections();
  }

  return snapshot();
}

SessionSnapshot Session::snapshot() const {
  SessionSnapshot current{
      .sources = sources_.sourceFiles(),
      .assets = assets_,
      .matchFacts = matchFacts_,
      .collections = collections_,
      .diagnostics = diagnostics_,
  };
  addMissingSequenceDialectDiagnostics(current, dialects_);
  finalizeSessionSnapshotIndex(current);
  return current;
}

std::vector<Artifact> Session::exportCollection(CollectionId id, const ExportRequest& request) const {
  const auto current = snapshot();
  return core::exportCollection(current, sources_, id, request, dialects_);
}

std::vector<CollectionExport> Session::exportAllCollections(const ExportRequest& request) const {
  const auto current = snapshot();
  return core::exportAllCollections(current, sources_, request, dialects_);
}

void Session::sealRegistries() noexcept {
  formats_.seal();
  dialects_.seal();
}

// Scan the requested source, then scan any sources extracted from it. This lets an
// archive or container produce bytes that normal format modules can parse.
void Session::scanSourceAndDerived(SourceId id) {
  if (!sources_.contains(id)) {
    throw std::out_of_range("Cannot scan a SourceId that is not present in the Session");
  }

  std::vector<SourceId> queue{id};
  std::set<u32> queued{id.value};

  for (size_t index = 0; index < queue.size(); ++index) {
    scanOneSource(queue[index], queue, queued);
  }
}

// Run every module that recognizes this source. Assets, match facts, diagnostics,
// and extracted sources are appended to the session.
void Session::scanOneSource(SourceId id, std::vector<SourceId>& queue, std::set<u32>& queued) {
  if (!scannedSources_.insert(id.value).second) {
    return;
  }

  const auto source = sources_.source(id);
  const auto bytes = sources_.bytes(id);

  for (const auto& module : formats_.modules()) {
    bool shouldScan = false;
    try {
      // Probe failures become diagnostics so one broken module cannot hide data
      // that another registered module can still parse.
      shouldScan = module.canScan(source, bytes);
    } catch (const std::exception& ex) {
      diagnostics_.push_back(sessionError(std::string(module.name) + " canScan failed: " + ex.what(),
                                          SourceRange{.source = source.id, .offset = 0, .size = source.size}));
    }

    if (!shouldScan) {
      continue;
    }

    try {
      ScanResult result = module.scan(ScanInput{
          .source = source,
          .reader = sources_.reader(id),
          .ids = ids_,
      });
      normalizeScanResult(result, ids_);
      validateScanResult(result);

      appendScanAssets(std::move(result.assets), source.id);
      matchFacts_.insert(matchFacts_.end(), std::make_move_iterator(result.matchFacts.begin()),
                         std::make_move_iterator(result.matchFacts.end()));
      for (auto& diagnostic : result.diagnostics) {
        if (!diagnostic.range) {
          diagnostic.range = SourceRange{.source = source.id, .offset = 0, .size = source.size};
        }
      }
      diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(result.diagnostics.begin()),
                          std::make_move_iterator(result.diagnostics.end()));

      for (auto& extracted : result.extractedSources) {
        SourceId parent = source.id;
        if (extracted.origin && extracted.origin->source.valid()) {
          if (!sources_.contains(extracted.origin->source)) {
            diagnostics_.push_back(
                sessionError(std::string(module.name) + " extracted source had a missing parent source",
                             SourceRange{.source = source.id, .offset = 0, .size = source.size}));
            continue;
          }
          parent = extracted.origin->source;
        }

        const SourceId derived =
            sources_.addDerived(std::move(extracted.file), std::move(extracted.bytes), parent, extracted.origin);
        if (queued.insert(derived.value).second) {
          queue.push_back(derived);
        }
      }
    } catch (const std::exception& ex) {
      diagnostics_.push_back(sessionError(std::string(module.name) + " scan failed: " + ex.what(),
                                          SourceRange{.source = source.id, .offset = 0, .size = source.size}));
    }
  }
}

void Session::validateScanResult(const ScanResult& result) const {
  std::unordered_set<u32> batchAssetIds;
  for (const auto& asset : result.assets) {
    const auto id = metadata(asset).id;
    if (!id.valid()) {
      throw std::invalid_argument("Scan result contained an asset without an id");
    }
    if (!batchAssetIds.insert(id.value).second) {
      throw std::invalid_argument("Scan result contained duplicate asset id " + std::to_string(id.value));
    }
    if (assetExists(id)) {
      throw std::invalid_argument("Scan result reused existing asset id " + std::to_string(id.value));
    }
  }

  for (const auto& fact : result.matchFacts) {
    if (!fact.asset.valid()) {
      throw std::invalid_argument("Scan result contained a match fact without an asset id");
    }
    if (!batchAssetIds.contains(fact.asset.value) && !assetExists(fact.asset)) {
      throw std::invalid_argument("Scan result contained a match fact for missing asset id " +
                                  std::to_string(fact.asset.value));
    }
    if (fact.scope.kind == MatchScopeKind::Source && !fact.scope.source) {
      throw std::invalid_argument("Scan result contained a source-scoped match fact without a source id");
    }
    if (fact.scope.source && !sources_.contains(*fact.scope.source)) {
      throw std::invalid_argument("Scan result contained a match fact for missing source id " +
                                  std::to_string(fact.scope.source->value));
    }
  }
}

bool Session::assetExists(AssetId id) const noexcept {
  return id.valid() && assetSourceOwners_.contains(id.value);
}

void Session::appendScanAssets(std::vector<Asset> assets, SourceId owner) {
  std::unordered_set<u32> batchIds;
  for (const auto& asset : assets) {
    const auto id = metadata(asset).id;
    if (!id.valid()) {
      throw std::invalid_argument("Scan result contained an asset without an id");
    }
    if (!batchIds.insert(id.value).second) {
      throw std::invalid_argument("Scan result contained duplicate asset id " + std::to_string(id.value));
    }
    if (assetSourceOwners_.contains(id.value)) {
      throw std::invalid_argument("Scan result reused existing asset id " + std::to_string(id.value));
    }
  }

  for (auto& asset : assets) {
    const auto id = metadata(asset).id;
    assetSourceOwners_.emplace(id.value, owner.value);
    assets_.push_back(std::move(asset));
  }
}

void Session::removeDiscoveredDataForSources(const std::vector<SourceId>& sources) {
  const auto sourceIds = sourceIdSet(sources);
  std::unordered_set<u32> removedAssetIds;

  for (const auto& [assetId, sourceId] : assetSourceOwners_) {
    if (sourceIds.contains(sourceId)) {
      removedAssetIds.insert(assetId);
    }
  }

  for (const auto& asset : assets_) {
    const auto& meta = metadata(asset);
    if (meta.range.valid() && sourceIds.contains(meta.range.source.value) && meta.id.valid()) {
      removedAssetIds.insert(meta.id.value);
    }
  }

  for (const auto& fact : matchFacts_) {
    if (!factFromSource(fact, sourceIds) || !fact.asset.valid()) {
      continue;
    }
    if (!assetSourceOwners_.contains(fact.asset.value)) {
      removedAssetIds.insert(fact.asset.value);
    }
  }

  std::erase_if(assets_, [&](const Asset& asset) {
    const auto id = metadata(asset).id;
    return id.valid() && removedAssetIds.contains(id.value);
  });
  for (const u32 id : removedAssetIds) {
    assetSourceOwners_.erase(id);
  }

  std::erase_if(matchFacts_, [&](const MatchFact& fact) {
    return factFromSource(fact, sourceIds) || (fact.asset.valid() && removedAssetIds.contains(fact.asset.value));
  });
  std::erase_if(diagnostics_,
                [&](const Diagnostic& diagnostic) { return diagnosticFromSource(diagnostic, sourceIds); });

  markCollectionsStaleForAssets(removedAssetIds);
}

// Ask registered formats which collections should exist for the current assets and
// match facts, then merge those answers into the session.
void Session::rebuildCollections() {
  const auto current = snapshot();

  MatchContext context{
      .sources = sources_,
      .snapshot = current,
  };

  std::map<std::string, std::vector<DesiredCollection>> desiredByResolver;
  std::set<std::string> failedResolvers;
  for (const auto& module : formats_.modules()) {
    if (module.resolveCollections == nullptr) {
      continue;
    }

    const std::string resolverId =
        !module.collectionResolverId.empty() ? std::string(module.collectionResolverId) : std::string(module.name);
    auto& desiredCollections = desiredByResolver[resolverId];
    try {
      auto desired = module.resolveCollections(context);
      desiredCollections.insert(desiredCollections.end(), std::make_move_iterator(desired.begin()),
                                std::make_move_iterator(desired.end()));
    } catch (const std::exception& ex) {
      failedResolvers.insert(resolverId);
      diagnostics_.push_back(sessionError(std::string(module.name) + " resolveCollections failed: " + ex.what()));
    }
  }

  for (auto& [resolverId, desiredCollections] : desiredByResolver) {
    if (failedResolvers.contains(resolverId)) {
      continue;
    }
    reconcileCollections(resolverId, std::move(desiredCollections));
  }
}

CollectionId Session::nextCollectionId() {
  CollectionId id;
  do {
    id = ids_.nextCollectionId();
  } while (std::ranges::any_of(collections_, [id](const Collection& collection) { return collection.id == id; }));
  return id;
}

// A resolver owns every discovered collection with its resolver id. Each pass
// replaces that resolver's desired set while preserving ids for keys that remain.
void Session::reconcileCollections(std::string_view resolverId, std::vector<DesiredCollection> desiredCollections) {
  std::set<std::string> seenKeys;
  for (auto& desired : desiredCollections) {
    if (desired.key.resolver.empty()) {
      desired.key.resolver = std::string(resolverId);
    }
    if (desired.key.resolver != resolverId) {
      diagnostics_.push_back(sessionError("Collection resolver '" + std::string(resolverId) +
                                          "' returned a collection for resolver '" + desired.key.resolver + "'"));
      continue;
    }
    if (desired.key.value.empty()) {
      diagnostics_.push_back(sessionError("Collection resolver '" + std::string(resolverId) +
                                          "' returned a collection with an empty key"));
      continue;
    }

    const auto key = collectionKeyString(desired.key);
    if (!seenKeys.insert(key).second) {
      diagnostics_.push_back(sessionError("Collection resolver '" + std::string(resolverId) +
                                          "' returned duplicate collection key '" + desired.key.value + "'"));
      continue;
    }

    validateDesiredCollectionReferences(resolverId, desired);

    const auto sameKey = [&](const Collection& collection) { return collection.key == desired.key; };
    if (auto found = std::ranges::find_if(collections_, sameKey); found != collections_.end()) {
      if (found->origin == CollectionOrigin::UserCreated) {
        found->status = CollectionStatus::Stale;
        continue;
      }
      found->name = desired.name;
      found->status = statusWithIssues(desired);
      found->origin = desired.origin;
      found->sequence = desired.sequence;
      found->instrumentSets = desired.instrumentSets;
      found->sampleCollections = desired.sampleCollections;
      found->miscAssets = desired.miscAssets;
      found->issues = std::move(desired.issues);
      continue;
    }

    collections_.push_back(Collection{
        .id = nextCollectionId(),
        .name = desired.name,
        .status = statusWithIssues(desired),
        .origin = desired.origin,
        .key = desired.key,
        .sequence = desired.sequence,
        .instrumentSets = desired.instrumentSets,
        .sampleCollections = desired.sampleCollections,
        .miscAssets = desired.miscAssets,
        .issues = std::move(desired.issues),
    });
  }

  std::erase_if(collections_, [&](const Collection& collection) {
    return collection.origin == CollectionOrigin::Discovered && collection.key.resolver == resolverId &&
           !seenKeys.contains(collectionKeyString(collection.key));
  });
}

void Session::validateDesiredCollectionReferences(std::string_view resolverId, DesiredCollection& desired) {
  const auto addMissingAssetDiagnostic = [&](AssetId id, std::string_view role) {
    diagnostics_.push_back(sessionError("Collection resolver '" + std::string(resolverId) + "' returned " +
                                        std::string(role) + " asset id " + std::to_string(id.value) +
                                        " that does not exist"));
    desired.issues.push_back(CollectionIssue{
        .severity = Severity::Error,
        .code = "missing-" + std::string(role),
        .message = "Collection references missing " + std::string(role) + " asset " + std::to_string(id.value),
        .asset = id,
    });
  };

  if (desired.sequence && !assetExists(*desired.sequence)) {
    addMissingAssetDiagnostic(*desired.sequence, "sequence");
    desired.sequence = std::nullopt;
  }

  const auto filterExistingAssets = [&](std::vector<AssetId>& ids, std::string_view role) {
    std::erase_if(ids, [&](AssetId id) {
      if (assetExists(id)) {
        return false;
      }
      addMissingAssetDiagnostic(id, role);
      return true;
    });
  };

  filterExistingAssets(desired.instrumentSets, "instrument-set");
  filterExistingAssets(desired.sampleCollections, "sample-collection");
  filterExistingAssets(desired.miscAssets, "misc");
}

void Session::markCollectionsStaleForAssets(const std::unordered_set<u32>& assetIds) {
  if (assetIds.empty()) {
    return;
  }

  for (auto& collection : collections_) {
    if (!referencesAnyAsset(collection, assetIds)) {
      continue;
    }

    collection.status = CollectionStatus::Stale;
    if (!hasIssueCode(collection, "removed-asset")) {
      collection.issues.push_back(CollectionIssue{
          .severity = Severity::Error,
          .code = "removed-asset",
          .message = "Collection references an asset from a removed source",
      });
    }
  }
}

}  // namespace vgmtrans::core

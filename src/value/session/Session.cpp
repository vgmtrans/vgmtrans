/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/Session.h"

#include "value/export/Export.h"
#include "value/scan/FormatModule.h"
#include "value/session/ScanCommit.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

void addMissingSequenceDialectDiagnostics(SessionSnapshotBuilder& snapshot, const SequenceDialectRegistry& dialects) {
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

}  // namespace

void Session::registerFormat(FormatDefinition definition) {
  formats_.add(std::move(definition.module));
  if (definition.sequenceDialect) {
    dialects_.add(std::move(*definition.sequenceDialect));
  }
}

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
  SessionSnapshotBuilder current{
      .sources = sources_.sourceFiles(),
      .assets = assets_.all(),
      .matchFacts = matchFacts_.all(),
      .collections = collections_.all(),
      .sourceMap = sourceMaps_.all(),
      .diagnostics = diagnostics_.all(),
  };
  addMissingSequenceDialectDiagnostics(current, dialects_);
  return current.finish();
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
    if (module.canScan != nullptr) {
      bool shouldScan = false;
      try {
        // Legacy probe failures become diagnostics so one broken module cannot
        // hide data another registered module can still parse.
        shouldScan = module.canScan(source, bytes);
      } catch (const std::exception& ex) {
        diagnostics_.addError(std::string(module.name) + " canScan failed: " + ex.what(),
                              SourceRange{.source = source.id, .offset = 0, .size = source.size});
      }
      if (!shouldScan) {
        continue;
      }
    }

    try {
      ScanResult result = module.scan(ScanInput{
          .source = source,
          .reader = sources_.reader(id),
          .ids = ids_,
      });
      normalizeScanResult(result, ids_);
      ScanCommit commit = ScanCommit::fromScanResult(source, std::move(result));
      commit.validate(sources_, assets_);
      commit.commit(assets_, matchFacts_, explicitCollections_, sourceMaps_, diagnostics_);
      addExtractedSources(std::move(commit.extractedSources), source.id, queue, queued);
    } catch (const std::exception& ex) {
      diagnostics_.addError(std::string(module.name) + " scan failed: " + ex.what(),
                            SourceRange{.source = source.id, .offset = 0, .size = source.size});
    }
  }
}

void Session::addExtractedSources(std::vector<ExtractedSource> extractedSources, SourceId defaultParent,
                                  std::vector<SourceId>& queue, std::set<u32>& queued) {
  for (auto& extracted : extractedSources) {
    SourceId parent = defaultParent;
    if (extracted.origin && extracted.origin->source.valid()) {
      parent = extracted.origin->source;
    }

    const SourceId derived =
        sources_.addDerived(std::move(extracted.file), std::move(extracted.bytes), parent, extracted.origin);
    if (queued.insert(derived.value).second) {
      queue.push_back(derived);
    }
  }
}

void Session::removeDiscoveredDataForSources(const std::vector<SourceId>& sources) {
  const auto removedAssetIds = assets_.removeForSources(sources);
  matchFacts_.removeForSourcesAndAssets(sources, removedAssetIds);
  explicitCollections_.removeForSourcesAndAssets(sources, removedAssetIds);
  sourceMaps_.removeForSources(sources);
  diagnostics_.removeForSources(sources);
  collections_.markStaleForAssets(removedAssetIds);
}

// Ask registered formats which collections should exist for the current assets and
// match facts, then merge those answers into the session.
void Session::rebuildCollections() {
  const auto current = snapshot();

  MatchContext context{
      .sources = sources_,
      .snapshot = current,
  };

  std::map<std::string, std::vector<DesiredCollection>> desiredByResolver = explicitCollections_.desiredByResolver();
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
      if (module.materializeCollection != nullptr) {
        // Materializers are format code and may fail on any collection. Apply
        // the resolver's whole batch to copies first so one late failure cannot
        // publish earlier derived assets, annotations, or diagnostics.
        auto stagedAssets = assets_;
        auto stagedSourceMaps = sourceMaps_;
        std::vector<Diagnostic> stagedDiagnostics;
        std::vector<DesiredCollection> materializedDesired;
        std::set<std::string> activeMaterializedKeys;
        for (auto& collection : desired) {
          if (collection.key.resolver.empty()) {
            collection.key.resolver = resolverId;
          }
          MaterializationContext materialization{
              .sources = sources_,
              .snapshot = current,
              .collection = collection,
              .ids = ids_,
              .assetIdForSlot =
                  [&](std::string_view slot) {
                    return stagedAssets.materializedAssetId(resolverId, collection.key, slot, ids_);
                  },
          };
          auto result = module.materializeCollection(materialization);
          stagedDiagnostics.insert(stagedDiagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                                   std::make_move_iterator(result.diagnostics.end()));
          std::vector<AssetId> materializedAssetIds;
          materializedAssetIds.reserve(result.assets.size());
          for (auto& asset : result.assets) {
            materializedAssetIds.push_back(metadata(asset.asset).id);
            activeMaterializedKeys.insert(
                stagedAssets.upsertMaterializedAsset(resolverId, collection.key, asset.slot, std::move(asset.asset)));
          }
          // A rebuilt derived asset keeps its ID but may point at different
          // source records, so its old annotations must not accumulate.
          stagedSourceMaps.replaceForAssets(materializedAssetIds, std::move(result.sourceMap));
          materializedDesired.push_back(std::move(result.collection));
        }
        const auto removedMaterializedAssets =
            stagedAssets.removeStaleMaterializedAssets(resolverId, activeMaterializedKeys);
        stagedSourceMaps.removeForAssets(removedMaterializedAssets);
        assets_ = std::move(stagedAssets);
        sourceMaps_ = std::move(stagedSourceMaps);
        diagnostics_.append(std::move(stagedDiagnostics));
        desired = std::move(materializedDesired);
      }
      desiredCollections.insert(desiredCollections.end(), std::make_move_iterator(desired.begin()),
                                std::make_move_iterator(desired.end()));
    } catch (const std::exception& ex) {
      failedResolvers.insert(resolverId);
      diagnostics_.addError(std::string(module.name) + " resolveCollections failed: " + ex.what());
    }
  }

  for (auto& [resolverId, desiredCollections] : desiredByResolver) {
    if (failedResolvers.contains(resolverId)) {
      continue;
    }
    collections_.reconcile(resolverId, std::move(desiredCollections), assets_, diagnostics_, ids_);
  }
}

}  // namespace vgmtrans::core

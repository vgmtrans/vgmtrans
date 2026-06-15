/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/Session.h"

#include "value/export/Export.h"
#include "value/scan/FormatModule.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
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

}  // namespace

SourceId Session::addSource(SourceFile file, std::vector<u8> bytes) {
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

SessionSnapshot Session::scan() {
  return rescanAll();
}

SessionSnapshot Session::scanSource(SourceId id) {
  scanSourceFamily(id, false);
  return snapshot();
}

SessionSnapshot Session::rescanSource(SourceId id) {
  scanSourceFamily(id, true);
  return snapshot();
}

SessionSnapshot Session::rescanAll() {
  assets_.clear();
  matchFacts_.clear();
  collections_.clear();
  diagnostics_.clear();
  ids_ = {};
  scannedSources_.clear();
  sources_.markDerivedSourcesStale();

  const size_t sourceCount = sources_.sourceCount();
  for (size_t index = 0; index < sourceCount; ++index) {
    const auto& source = sources_.sourceAt(index);
    if (!source.derived()) {
      scanSourceFamily(source.id, false);
    }
  }

  return snapshot();
}

SessionSnapshot Session::snapshot() const {
  SessionSnapshot current{
      .sources = sources_.sourceFiles(),
      .assets = assets_.snapshot(),
      .matchFacts = matchFacts_.snapshot(),
      .collections = collections_.snapshot(),
      .diagnostics = diagnostics_.snapshot(),
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

void Session::scanSourceFamily(SourceId id, bool clearExisting) {
  if (!sources_.contains(id)) {
    throw std::out_of_range("Cannot scan a SourceId that is not present in the Session");
  }

  if (clearExisting) {
    sources_.markDerivedSourceFamilyStale(id);
    removeDiscoveredDataForSourceFamily(id);
  }

  const u32 loadGroup = nextLoadGroup_++;
  std::vector<SourceId> queue{id};
  std::set<u32> queued{id.value};

  for (size_t index = 0; index < queue.size(); ++index) {
    scanOneSource(queue[index], loadGroup, queue, queued);
  }

  rebuildCollections();
}

void Session::scanOneSource(SourceId id, u32 loadGroup, std::vector<SourceId>& queue, std::set<u32>& queued) {
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
      diagnostics_.add(sessionError(std::string(module.name) + " canScan failed: " + ex.what()));
    }

    if (!shouldScan) {
      continue;
    }

    try {
      ScanResult result = module.scan(ScanInput{
          .source = source,
          .reader = sources_.reader(id),
          .ids = ids_,
          .loadGroup = loadGroup,
      });
      normalizeScanResult(result, ids_);

      const auto upsert = assets_.upsertDiscovered(std::move(result.assets));
      matchFacts_.add(std::move(result.matchFacts), upsert.remappedIds);
      diagnostics_.add(std::move(result.diagnostics));

      for (auto& extracted : result.extractedSources) {
        const SourceId parent =
            extracted.origin && extracted.origin->source.valid() ? extracted.origin->source : source.id;
        std::string derivedKey = extracted.file.derivedKey;
        if (derivedKey.empty()) {
          derivedKey = !extracted.file.name.empty() ? extracted.file.name : std::string(module.name);
        }

        const SourceId derived =
            sources_.addOrUpdateDerived(std::move(extracted.file), std::move(extracted.bytes), parent,
                                        std::string(module.name), std::move(derivedKey), extracted.origin);
        if (queued.insert(derived.value).second) {
          queue.push_back(derived);
        }
      }
    } catch (const std::exception& ex) {
      diagnostics_.add(sessionError(std::string(module.name) + " scan failed: " + ex.what(),
                                    SourceRange{.source = source.id, .offset = 0, .size = source.size}));
    }
  }
}

void Session::removeDiscoveredDataForSourceFamily(SourceId id) {
  const auto family = sources_.sourceFamily(id);
  std::unordered_set<u32> sourceIds;
  sourceIds.reserve(family.size());
  for (const auto source : family) {
    sourceIds.insert(source.value);
    scannedSources_.erase(source.value);
  }

  const auto assetsFromFacts = matchFacts_.assetIdsForSources(sourceIds);
  const auto removedAssets = assets_.removeForSources(sourceIds, assetsFromFacts);
  matchFacts_.removeForSourcesOrAssets(sourceIds, removedAssets);
  diagnostics_.removeForSources(sourceIds);
  collections_.markReferencesStale(removedAssets);
}

void Session::rebuildCollections() {
  const auto current = snapshot();

  MatchContext context{
      .sources = sources_,
      .snapshot = current,
  };

  std::vector<DesiredCollection> desiredCollections;
  std::set<std::string> activeResolvers;
  for (const auto& module : formats_.modules()) {
    if (module.resolveCollections == nullptr) {
      continue;
    }

    const std::string_view resolver = !module.collectionResolver.empty() ? module.collectionResolver : module.name;
    activeResolvers.insert(std::string(resolver));
    auto desired = module.resolveCollections(context);
    desiredCollections.insert(desiredCollections.end(), std::make_move_iterator(desired.begin()),
                              std::make_move_iterator(desired.end()));
  }

  collections_.reconcile(std::move(desiredCollections), activeResolvers, ids_);
}

}  // namespace vgmtrans::core

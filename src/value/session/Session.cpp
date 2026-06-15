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

SessionSnapshot Session::scanSource(SourceId id) {
  if (!sources_.contains(id)) {
    throw std::out_of_range("Cannot scan a SourceId that is not present in the Session");
  }

  if (scannedSources_.contains(id.value)) {
    return snapshot();
  }

  scanSourceAndDerived(id, nextLoadGroup_++);
  rebuildCollections();
  return snapshot();
}

SessionSnapshot Session::scanPendingSources() {
  bool scannedAny = false;
  const size_t sourceCount = sources_.sourceCount();
  for (size_t index = 0; index < sourceCount; ++index) {
    const auto& source = sources_.sourceAt(index);
    if (source.derived() || scannedSources_.contains(source.id.value)) {
      continue;
    }

    scanSourceAndDerived(source.id, nextLoadGroup_++);
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

void Session::scanSourceAndDerived(SourceId id, u32 loadGroup) {
  if (!sources_.contains(id)) {
    throw std::out_of_range("Cannot scan a SourceId that is not present in the Session");
  }

  std::vector<SourceId> queue{id};
  std::set<u32> queued{id.value};

  for (size_t index = 0; index < queue.size(); ++index) {
    scanOneSource(queue[index], loadGroup, queue, queued);
  }
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
      diagnostics_.push_back(sessionError(std::string(module.name) + " canScan failed: " + ex.what()));
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

      assets_.insert(assets_.end(), std::make_move_iterator(result.assets.begin()),
                     std::make_move_iterator(result.assets.end()));
      matchFacts_.insert(matchFacts_.end(), std::make_move_iterator(result.matchFacts.begin()),
                         std::make_move_iterator(result.matchFacts.end()));
      diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(result.diagnostics.begin()),
                          std::make_move_iterator(result.diagnostics.end()));

      for (auto& extracted : result.extractedSources) {
        const SourceId parent =
            extracted.origin && extracted.origin->source.valid() ? extracted.origin->source : source.id;
        std::string derivedKey = extracted.file.derivedKey;
        if (derivedKey.empty()) {
          derivedKey = !extracted.file.name.empty() ? extracted.file.name : std::string(module.name);
        }

        const SourceId derived = sources_.addDerived(std::move(extracted.file), std::move(extracted.bytes), parent,
                                                     std::string(module.name), std::move(derivedKey), extracted.origin);
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

void Session::rebuildCollections() {
  const auto current = snapshot();

  MatchContext context{
      .sources = sources_,
      .snapshot = current,
  };

  std::vector<DesiredCollection> desiredCollections;
  for (const auto& module : formats_.modules()) {
    if (module.resolveCollections == nullptr) {
      continue;
    }

    auto desired = module.resolveCollections(context);
    desiredCollections.insert(desiredCollections.end(), std::make_move_iterator(desired.begin()),
                              std::make_move_iterator(desired.end()));
  }

  reconcileCollections(std::move(desiredCollections));
}

void Session::reconcileCollections(std::vector<DesiredCollection> desiredCollections) {
  for (const auto& desired : desiredCollections) {
    if (desired.key.resolver.empty() || desired.key.value.empty()) {
      continue;
    }

    const auto sameKey = [&](const Collection& collection) { return collection.key == desired.key; };
    if (auto found = std::ranges::find_if(collections_, sameKey); found != collections_.end()) {
      found->name = desired.name;
      found->status = desired.status;
      found->sequence = desired.sequence;
      found->instrumentSets = desired.instrumentSets;
      found->sampleCollections = desired.sampleCollections;
      found->miscAssets = desired.miscAssets;
      continue;
    }

    collections_.push_back(Collection{
        .id = ids_.nextCollectionId(),
        .name = desired.name,
        .status = desired.status,
        .key = desired.key,
        .sequence = desired.sequence,
        .instrumentSets = desired.instrumentSets,
        .sampleCollections = desired.sampleCollections,
        .miscAssets = desired.miscAssets,
    });
  }
}

}  // namespace vgmtrans::core

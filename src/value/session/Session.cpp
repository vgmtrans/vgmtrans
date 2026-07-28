/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/Session.h"

#include "value/export/Export.h"
#include "value/model/SessionSnapshotAccess.h"
#include "value/scan/FormatModule.h"
#include "value/session/SessionState.h"
#include "value/validation/ScanValidation.h"

#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

void prepareDiagnostics(ScanResult& result, const SourceFile& source, const SequenceDialectRegistry& dialects) {
  for (const auto& asset : result.assets) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || dialects.contains(sequence->program.dialect.value)) {
      continue;
    }

    result.diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .message = "No sequence dialect registered for '" + sequence->program.dialect.value + "'",
        .range = sequence->metadata.range.valid() ? std::optional<SourceRange>{sequence->metadata.range} : std::nullopt,
    });
  }
  for (auto& diagnostic : result.diagnostics) {
    if (!diagnostic.range) {
      diagnostic.range = SourceRange{.source = source.id, .offset = 0, .size = source.size};
    }
  }
}

}  // namespace

Session::Session() : state_(std::make_unique<SessionState>()) {
}

Session::~Session() = default;
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;

void Session::registerFormat(FormatDefinition definition) {
  formats_.add(std::move(definition.module));
  for (auto& dialect : definition.sequenceDialects) {
    dialects_.add(std::move(dialect));
  }
}

void Session::registerFormat(FormatModule module) {
  registerFormat(FormatDefinition{.module = std::move(module)});
}

void Session::registerFormat(FormatModule module, SequenceDialect dialect) {
  registerFormat(FormatDefinition{.module = std::move(module), .sequenceDialects = {std::move(dialect)}});
}

SourceId Session::addSource(SourceFile file, std::vector<u8> bytes) {
  sealRegistries();
  invalidateSnapshot();
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

void Session::removeSource(SourceId id) {
  const std::array ids{id};
  removeSources(ids);
}

void Session::removeSources(std::span<const SourceId> ids) {
  sealRegistries();
  for (const SourceId id : ids) {
    if (!sources_.hasSlot(id)) {
      throw std::out_of_range("Cannot remove a SourceId that is not present in the Session");
    }
  }

  std::vector<SourceId> removed;
  for (const SourceId id : ids) {
    if (sources_.contains(id)) {
      if (removed.empty()) {
        invalidateSnapshot();
      }
      removeSourceFamily(id, removed);
    }
  }
  if (removed.empty()) {
    return;
  }

  state_->removeSources(removed);
  rebuildCollections();
}

void Session::removeAssets(std::span<const AssetId> assets) {
  sealRegistries();
  if (!std::ranges::any_of(assets, [&](AssetId id) { return state_->containsAsset(id); })) {
    return;
  }

  std::vector<SourceId> affectedRoots;
  for (const AssetId id : assets) {
    const auto* asset = state_->asset(id);
    SourceId source = asset != nullptr ? metadata(*asset).range.source : SourceId{};
    if (!sources_.contains(source)) {
      continue;
    }
    while (const auto parent = sources_.source(source).parent) {
      source = *parent;
    }
    if (std::ranges::find(affectedRoots, source) == affectedRoots.end()) {
      affectedRoots.push_back(source);
    }
  }

  invalidateSnapshot();
  static_cast<void>(state_->removeAssets(assets));

  std::vector<SourceId> removedSources;
  for (const SourceId root : affectedRoots) {
    const auto family = sources_.sourceFamily(root);
    const bool hasAssets = std::ranges::any_of(state_->assets(), [&](const Asset& asset) {
      return std::ranges::find(family, metadata(asset).range.source) != family.end();
    });
    if (hasAssets) {
      continue;
    }
    removeSourceFamily(root, removedSources);
  }
  if (!removedSources.empty()) {
    state_->removeSources(removedSources);
  }

  rebuildCollections();
}

// Scan this source if it has not been scanned yet. Any files extracted from it are
// added as derived sources and scanned before this call returns.
void Session::scanSource(SourceId id) {
  sealRegistries();
  if (!sources_.contains(id)) {
    throw std::out_of_range("Cannot scan a SourceId that is not present in the Session");
  }

  if (scannedSources_.contains(id.value)) {
    return;
  }

  invalidateSnapshot();
  scanSourceAndDerived(id);
  rebuildCollections();
}

// Scan every user-loaded source that is still pending. Derived sources are skipped
// here because scanning their parent source already scans them.
void Session::scanPendingSources() {
  sealRegistries();
  bool scannedAny = false;
  for (const SourceId source : sources_.activeUserSources()) {
    if (scannedSources_.contains(source.value)) {
      continue;
    }

    if (!scannedAny) {
      invalidateSnapshot();
    }
    scanSourceAndDerived(source);
    scannedAny = true;
  }

  if (scannedAny) {
    rebuildCollections();
  }
}

SessionSnapshot Session::snapshot() const {
  if (!snapshotCache_) {
    snapshotCache_.emplace(detail::SessionSnapshotAccess::create(sources_.sourceFiles(), state_->assets(),
                                                                 state_->matchFacts(), state_->collections(),
                                                                 state_->sourceMap(), state_->diagnostics()));
  }
  return *snapshotCache_;
}

std::shared_ptr<const SourceInspection> Session::inspect(AssetId asset) const {
  const auto* value = state_->asset(asset);
  if (value == nullptr) {
    return {};
  }
  const SourceId source = metadata(*value).range.source;
  if (!sources_.contains(source)) {
    return {};
  }
  return SourceInspection::create(metadata(*value), state_->sourceMapForAsset(asset), sources_.sharedBytes(source));
}

CollectionPlayback Session::preparePlayback(CollectionId id, const PlaybackRequest& request) const {
  const auto current = snapshot();
  return core::prepareCollectionPlayback(current, sources_, id, request, dialects_, &formats_);
}

Artifact Session::exportSequenceMidi(AssetId id, const SequenceExportRequest& request) const {
  return core::exportSequenceMidi(snapshot(), sources_, id, request, dialects_, &formats_);
}

Artifact Session::exportInstrumentSet(AssetId id, SynthExportFormat format, const ExportRequest& request) const {
  return core::exportInstrumentSet(snapshot(), sources_, id, format, request, dialects_, &formats_);
}

std::vector<Artifact> Session::exportCollection(CollectionId id, const ExportRequest& request) const {
  const auto current = snapshot();
  return core::exportCollection(current, sources_, id, request, dialects_, &formats_);
}

std::vector<CollectionExport> Session::exportAllCollections(const ExportRequest& request) const {
  const auto current = snapshot();
  return core::exportAllCollections(current, sources_, request, dialects_, &formats_);
}

void Session::sealRegistries() noexcept {
  formats_.seal();
  dialects_.seal();
}

void Session::invalidateSnapshot() noexcept {
  snapshotCache_.reset();
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
    if (module.canScan) {
      bool shouldScan = false;
      try {
        // Legacy probe failures become diagnostics so one broken module cannot
        // hide data another registered module can still parse.
        shouldScan = module.canScan(source, bytes);
      } catch (const std::exception& ex) {
        state_->addError(std::string(module.name) + " canScan failed: " + ex.what(),
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
      prepareDiagnostics(result, source, dialects_);
      auto validation = validateScanResult(source.id, result, sources_, state_->assets());
      if (!validation.empty()) {
        auto diagnostics = validation.takeDiagnostics();
        for (auto& diagnostic : diagnostics) {
          diagnostic.message = std::string(module.name) + " scan failed: " + diagnostic.message;
          if (!diagnostic.range || !sources_.contains(diagnostic.range->source)) {
            diagnostic.range = SourceRange{.source = source.id, .offset = 0, .size = source.size};
          }
        }
        state_->addDiagnostics(std::move(diagnostics));
        continue;
      }
      auto extractedSources = std::exchange(result.extractedSources, {});
      state_->appendScan(source.id, std::move(result));
      addExtractedSources(std::move(extractedSources), source.id, queue, queued);
    } catch (const std::exception& ex) {
      state_->addError(std::string(module.name) + " scan failed: " + ex.what(),
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

void Session::removeSourceFamily(SourceId source, std::vector<SourceId>& removedSources) {
  auto family = sources_.removeFamily(source);
  for (const SourceId removed : family) {
    scannedSources_.erase(removed.value);
  }
  removedSources.insert(removedSources.end(), family.begin(), family.end());
}

// Ask registered formats which collections should exist for the current assets and
// match facts, then merge those answers into the session.
void Session::rebuildCollections() {
  const MatchContext context{sources_, state_->assets(), state_->matchFacts()};

  auto desiredByResolver = state_->desiredCollectionsByResolver();
  std::set<std::string> failedResolvers;
  for (const auto& module : formats_.modules()) {
    if (!module.resolveCollections) {
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
      state_->addError(std::string(module.name) + " resolveCollections failed: " + ex.what());
    }
  }

  for (auto& [resolverId, desiredCollections] : desiredByResolver) {
    if (failedResolvers.contains(resolverId)) {
      continue;
    }
    state_->reconcileCollections(resolverId, std::move(desiredCollections), ids_);
  }
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/Session.h"

#include "value/export/CollectionStitch.h"
#include "value/export/Export.h"
#include "value/model/SessionSnapshotAccess.h"
#include "value/scan/CollectionDiscovery.h"
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

void prepareDiagnosticRanges(std::vector<Diagnostic>& diagnostics, const SourceFile& source) {
  for (auto& diagnostic : diagnostics) {
    if (!diagnostic.range) {
      diagnostic.range = SourceRange{.source = source.id, .offset = 0, .size = source.size};
    }
  }
}

void prepareDiagnostics(ScanResult& result, const SourceFile& source) {
  for (const auto& asset : result.assets) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || sequence->program.runtime.valid()) {
      continue;
    }

    result.diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .message = "Sequence program has no runtime executor",
        .range = sequence->metadata.range.valid() ? std::optional<SourceRange>{sequence->metadata.range} : std::nullopt,
    });
  }
  prepareDiagnosticRanges(result.diagnostics, source);
}

[[nodiscard]] bool accepts(const SourceFile& source, const std::vector<std::string>& acceptedFormats) {
  return !source.knownFormat || std::ranges::find(acceptedFormats, *source.knownFormat) != acceptedFormats.end();
}

}  // namespace

Session::Session() : state_(std::make_unique<SessionState>()) {
}

Session::~Session() = default;
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;

void Session::registerFormat(FormatModule module) {
  formats_.add(std::move(module));
}

void Session::registerExtractor(SourceExtractor extractor) {
  formats_.add(std::move(extractor));
}

SourceId Session::addSource(SourceFile file, std::vector<u8> bytes) {
  if (file.knownFormat && file.knownFormat->empty()) {
    throw std::invalid_argument("Cannot add a source with an empty known format");
  }
  sealFormats();
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
  sealFormats();
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
  sealFormats();
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

CollectionId Session::createUserCollection(std::string name, CollectionMembers members) {
  sealFormats();
  CollectionBinder binder;
  if (members.sequence) {
    const auto* sequence = state_->asset<SequenceProgramAsset>(*members.sequence);
    if (sequence != nullptr) {
      binder = formats_.collectionBinderForFormat(sequence->metadata.format);
    }
  }
  const CollectionId id = state_->createUserCollection(std::move(name), std::move(members), std::move(binder), ids_);
  invalidateSnapshot();
  return id;
}

// Scan this source if it has not been scanned yet. Any files extracted from it are
// added as derived sources and scanned before this call returns.
void Session::scanSource(SourceId id) {
  sealFormats();
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
  sealFormats();
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
    snapshotCache_.emplace(detail::SessionSnapshotAccess::create(
        sources_.sourceFiles(), state_->assets(), state_->collections(), state_->sourceMap(), state_->diagnostics()));
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
  return core::prepareCollectionPlayback(current, sources_, id, request);
}

Artifact Session::exportSequenceMidi(AssetId id, const SequenceExportRequest& request) const {
  return core::exportSequenceMidi(snapshot(), sources_, id, request);
}

Artifact Session::exportSoundBank(AssetId id, SynthExportFormat format, const ExportRequest& request) const {
  return core::exportSoundBank(snapshot(), sources_, id, format, request);
}

std::vector<Artifact> Session::exportSamples(AssetId id) const {
  return core::exportSamples(snapshot(), sources_, id);
}

std::vector<Artifact> Session::exportCollection(CollectionId id, const ExportRequest& request) const {
  const auto current = snapshot();
  return core::exportCollection(current, sources_, id, request);
}

std::vector<CollectionExport> Session::exportAllCollections(const ExportRequest& request) const {
  const auto current = snapshot();
  return core::exportAllCollections(current, sources_, request);
}

CollectionStitchResult Session::stitchCollections(std::span<const CollectionId> collections,
                                                  const ExportRequest& request) const {
  return core::stitchCollections(snapshot(), sources_, collections, request);
}

void Session::sealFormats() noexcept {
  formats_.seal();
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

  const size_t assetsBefore = state_->assets().size();
  const size_t diagnosticsBefore = state_->diagnostics().size();
  std::vector<SourceId> queue{id};
  std::set<u32> queued{id.value};

  for (size_t index = 0; index < queue.size(); ++index) {
    scanOneSource(queue[index], queue, queued);
  }

  // Diagnostic-only scans remain open so their failures retain valid source
  // ranges. A scan that produced nothing can release its complete source family.
  if (state_->assets().size() == assetsBefore && state_->diagnostics().size() == diagnosticsBefore) {
    std::vector<SourceId> removed;
    removeSourceFamily(id, removed);
    state_->removeSources(removed);
  }
}

// Unknown sources use normal discovery. A known format restricts both stages to
// processors that advertise that representation, independent of source origin.
void Session::scanOneSource(SourceId id, std::vector<SourceId>& queue, std::set<u32>& queued) {
  if (!scannedSources_.insert(id.value).second) {
    return;
  }

  const auto source = sources_.source(id);
  bool acceptsKnownFormat = false;
  const auto addValidationFailure = [&](std::string_view processor, std::string_view operation,
                                        ValidationReport validation) {
    auto diagnostics = validation.takeDiagnostics();
    for (auto& diagnostic : diagnostics) {
      diagnostic.message = std::string(processor) + " " + std::string(operation) + " failed: " + diagnostic.message;
      if (!diagnostic.range || !sources_.contains(diagnostic.range->source)) {
        diagnostic.range = SourceRange{.source = source.id, .offset = 0, .size = source.size};
      }
    }
    state_->addDiagnostics(std::move(diagnostics));
  };

  // Extraction is a distinct first stage. Producing valid children means the
  // extractor has consumed the input, so ordinary scanners only see the children.
  for (const auto& extractor : formats_.extractors()) {
    if (!accepts(source, extractor.acceptedFormats)) {
      continue;
    }
    acceptsKnownFormat = true;
    try {
      ExtractionResult result = extractor.extract(ExtractionInput{
          .source = source,
          .reader = sources_.reader(id),
      });
      prepareDiagnosticRanges(result.diagnostics, source);
      auto validation = validateExtractionResult(source.id, result, sources_);
      if (!validation.empty()) {
        addValidationFailure(extractor.name, "extraction", std::move(validation));
        continue;
      }

      const bool consumed = !result.sources.empty();
      state_->addDiagnostics(std::move(result.diagnostics));
      if (consumed) {
        addExtractedSources(std::move(result.sources), source.id, queue, queued);
        return;
      }
    } catch (const std::exception& ex) {
      state_->addError(std::string(extractor.name) + " extraction failed: " + ex.what(),
                       SourceRange{.source = source.id, .offset = 0, .size = source.size});
    }
  }

  const auto scanModule = [&](const FormatModule& module) {
    try {
      ScanResult result = module.scan(ScanInput{
          .source = source,
          .reader = sources_.reader(id),
          .ids = ids_,
      });
      for (auto& asset : result.assets) {
        if (auto* bank = std::get_if<SoundBankAsset>(&asset)) {
          bank->localSamples.preferredFilter = module.preferredSampleFilter;
        } else if (auto* samples = std::get_if<SamplePoolAsset>(&asset)) {
          samples->pool.preferredFilter = module.preferredSampleFilter;
        }
      }
      normalizeScanResult(result, ids_);
      prepareDiagnostics(result, source);
      auto validation = validateScanResult(source.id, result, sources_, state_->assets());
      if (!validation.empty()) {
        addValidationFailure(module.name, "scan", std::move(validation));
        return;
      }
      state_->appendScan(source.id, std::move(result));
    } catch (const std::exception& ex) {
      state_->addError(std::string(module.name) + " scan failed: " + ex.what(),
                       SourceRange{.source = source.id, .offset = 0, .size = source.size});
    }
  };

  for (const auto& module : formats_.modules()) {
    if (!accepts(source, module.acceptedFormats)) {
      continue;
    }
    acceptsKnownFormat = true;
    scanModule(module);
  }

  if (source.knownFormat && !acceptsKnownFormat) {
    state_->addDiagnostics({Diagnostic{
        .severity = Severity::Error,
        .code = "scan.known-format.unsupported",
        .message = "No registered processor accepts known source format '" + *source.knownFormat + "'",
        .range = SourceRange{.source = source.id, .offset = 0, .size = source.size},
    }});
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

// Ask registered formats which collections should exist for the current assets,
// then merge those answers into the session.
void Session::rebuildCollections() {
  const CollectionDiscoveryContext context{sources_, state_->assets()};

  auto desiredByResolver = state_->desiredCollectionsByResolver();
  std::set<std::string> failedResolvers;
  for (const auto& module : formats_.modules()) {
    if (!module.resolveCollections) {
      continue;
    }

    const std::string resolverId(module.collectionResolver());
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
    state_->reconcileCollections(resolverId, std::move(desiredCollections), formats_.collectionBinder(resolverId),
                                 ids_);
  }
}

}  // namespace vgmtrans::core

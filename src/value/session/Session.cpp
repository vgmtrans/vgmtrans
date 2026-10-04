/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/Session.h"

#include "value/export/CollectionStitch.h"
#include "value/export/Export.h"
#include "value/scan/AssetCatalog.h"
#include "value/scan/AssetResolution.h"
#include "value/scan/FormatModule.h"
#include "value/session/SessionState.h"
#include "value/validation/ScanValidation.h"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <exception>
#include <future>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vgmtrans::core {

namespace {

void validateAcceptedFormats(std::string_view processorName, const std::vector<std::string>& formats) {
  std::unordered_set<std::string_view> provided;
  provided.reserve(formats.size());
  for (const auto& format : formats) {
    if (format.empty()) {
      throw std::invalid_argument("Cannot register an empty accepted source format");
    }
    if (!provided.insert(format).second) {
      throw std::invalid_argument(fmt::format("Duplicate source format accepted by {}: {}", processorName, format));
    }
  }
}

void prepareDiagnosticRanges(std::vector<Diagnostic>& diagnostics, const SourceFile& source) {
  for (auto& diagnostic : diagnostics) {
    if (!diagnostic.range.valid()) {
      diagnostic.range = SourceRange{.source = source.id, .offset = 0, .size = source.size};
    }
  }
}

[[nodiscard]] bool accepts(const SourceFile& source, const std::vector<std::string>& acceptedFormats) {
  return !source.knownFormat || std::ranges::find(acceptedFormats, *source.knownFormat) != acceptedFormats.end();
}

struct FormatScan {
  const FormatModule& format;
  ScanResult result;
};

void runFormatScans(std::span<FormatScan> scans, const ScanInput& input) {
  if (scans.empty()) {
    return;
  }

  const unsigned available = std::thread::hardware_concurrency();
  const size_t workerLimit = available == 0 ? 2 : available;
  const size_t workerCount = std::min(scans.size(), workerLimit);

  std::atomic_size_t nextTask{0};
  const auto work = [&] {
    while (true) {
      const size_t index = nextTask.fetch_add(1, std::memory_order_relaxed);
      if (index >= scans.size()) {
        return;
      }
      auto& scan = scans[index];
      const auto fail = [&](std::string message) {
        scan.result.diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .message = scan.format.name + " scan failed: " + message,
            .range = SourceRange{.source = input.source.id, .offset = 0, .size = input.source.size},
        });
      };
      try {
        scan.result = scan.format.scan(input);
      } catch (const std::exception& ex) {
        fail(ex.what());
      } catch (...) {
        fail("Unknown exception");
      }
    }
  };

  std::vector<std::future<void>> workers;
  workers.reserve(workerCount - 1);
  for (size_t index = 1; index < workerCount; ++index) {
    workers.push_back(std::async(std::launch::async, work));
  }
  work();
  for (auto& worker : workers) {
    worker.get();
  }
}

}  // namespace

Session::Session() : state_(std::make_unique<SessionState>()) {
}

Session::~Session() = default;
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;

void Session::registerFormat(FormatModule module) {
  if (formatsSealed_) {
    throw std::logic_error("Cannot register value formats after session mutation has started");
  }
  if (module.name.empty() || !module.scan) {
    throw std::invalid_argument("Cannot register an incomplete FormatModule");
  }
  if (std::ranges::find(formats_, module.name, &FormatModule::name) != formats_.end()) {
    throw std::invalid_argument(fmt::format("Duplicate FormatModule name: {}", module.name));
  }
  validateAcceptedFormats(module.name, module.acceptedFormats);
  formats_.push_back(std::move(module));
}

void Session::registerExtractor(SourceExtractor extractor) {
  if (formatsSealed_) {
    throw std::logic_error("Cannot register source extractors after session mutation has started");
  }
  if (extractor.name.empty() || !extractor.extract) {
    throw std::invalid_argument("Cannot register an incomplete SourceExtractor");
  }
  if (std::ranges::find(extractors_, extractor.name, &SourceExtractor::name) != extractors_.end()) {
    throw std::invalid_argument(fmt::format("Duplicate SourceExtractor name: {}", extractor.name));
  }
  validateAcceptedFormats(extractor.name, extractor.acceptedFormats);
  extractors_.push_back(std::move(extractor));
}

SourceId Session::addSource(SourceFile file, std::vector<u8> bytes) {
  if (file.knownFormat && file.knownFormat->empty()) {
    throw std::invalid_argument("Cannot add a source with an empty known format");
  }
  if (const auto existing = sources_.findFile(file.path)) {
    return *existing;
  }
  formatsSealed_ = true;
  snapshotCache_.reset();
  file.parent.reset();
  return sources_.add(std::move(file), std::move(bytes));
}

SourceFile Session::resolveSource(const std::filesystem::path& path, std::optional<SourceId> parent) const {
  SourceFile source{.path = path};
  for (const auto& extractor : extractors_) {
    if (extractor.resolvePath) {
      if (auto resolved = extractor.resolvePath(path, [&](const auto& candidate) {
            return parent ? sources_.findFile(candidate, parent).has_value()
                          : std::filesystem::is_regular_file(candidate);
          })) {
        source = std::move(*resolved);
        break;
      }
    }
  }
  if (const auto id = sources_.findFile(source.path, parent)) {
    auto existing = sources_.source(*id);
    existing.knownFormat = source.knownFormat ? source.knownFormat : existing.knownFormat;
    return existing;
  }
  if (parent) {
    throw std::runtime_error("Resolved archive member is missing: " + source.path.string());
  }
  source.id = {};
  return source;
}

SourceId Session::addSourceFromPath(std::filesystem::path path) {
  auto source = resolveSource(path);
  if (source.id.valid()) {
    return source.id;
  }
  auto bytes = readFileBytes(source.path);
  return addSource(std::move(source), std::move(bytes));
}

void Session::removeSource(SourceId id) {
  const std::array ids{id};
  removeSources(ids);
}

void Session::removeSources(std::span<const SourceId> ids) {
  formatsSealed_ = true;
  for (const SourceId id : ids) {
    if (!sources_.hasSlot(id)) {
      throw std::out_of_range("Cannot remove a SourceId that is not present in the Session");
    }
  }

  std::vector<SourceId> removed;
  for (const SourceId id : ids) {
    if (sources_.contains(id)) {
      if (removed.empty()) {
        snapshotCache_.reset();
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
  formatsSealed_ = true;
  if (!std::ranges::any_of(assets, [&](AssetId id) { return state_->asset(id) != nullptr; })) {
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

  snapshotCache_.reset();
  state_->removeAssets(assets);

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
  formatsSealed_ = true;
  Collection selection{.name = std::move(name), .selection = std::move(members)};
  resolveDependencies(AssetCatalog{sources_, state_->assets()}, selection, ResolutionMode::Manual);
  const CollectionId id = state_->createUserCollection(std::move(selection));
  snapshotCache_.reset();
  return id;
}

// Scan this source if it has not been scanned yet. Any files extracted from it are
// added as derived sources and scanned before this call returns.
void Session::scanSource(SourceId id) {
  formatsSealed_ = true;
  if (!sources_.contains(id)) {
    throw std::out_of_range("Cannot scan a SourceId that is not present in the Session");
  }

  if (scannedSources_.contains(id)) {
    return;
  }

  snapshotCache_.reset();
  scanSourceAndDerived(id);
  rebuildCollections();
}

// Scan every user-loaded source that is still pending. Derived sources are skipped
// here because scanning their parent source already scans them.
void Session::scanPendingSources() {
  formatsSealed_ = true;
  bool scannedAny = false;
  for (const SourceId source : sources_.activeUserSources()) {
    if (scannedSources_.contains(source)) {
      continue;
    }

    if (!scannedAny) {
      snapshotCache_.reset();
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
    snapshotCache_.emplace(
        sources_.sourceFiles(), state_->assets(), state_->collections(), state_->sourceMap(), state_->diagnostics());
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
  return std::shared_ptr<const SourceInspection>(
      new SourceInspection(metadata(*value), state_->sourceMap(), sources_.sharedBytes(source)));
}

CollectionPlayback Session::preparePlayback(CollectionId id, const PlaybackRequest& request) const {
  return core::prepareCollectionPlayback(snapshot(), sources_, id, request);
}

Artifact Session::exportSequenceMidi(AssetId id, const SequenceRenderOptions& request) const {
  return core::exportSequenceMidi(snapshot(), sources_, id, request);
}

Artifact Session::exportSoundBank(AssetId id, SynthExportFormat format, const ExportRequest& request) const {
  return core::exportSoundBank(snapshot(), sources_, id, format, request);
}

std::vector<Artifact> Session::exportSamples(AssetId id) const {
  return core::exportSamples(snapshot(), sources_, id);
}

std::vector<Artifact> Session::exportCollection(CollectionId id, const ExportRequest& request) const {
  return core::exportCollection(snapshot(), sources_, id, request);
}

std::vector<CollectionExport> Session::exportAllCollections(const ExportRequest& request) const {
  return core::exportAllCollections(snapshot(), sources_, request);
}

CollectionStitchResult Session::stitchCollections(std::span<const CollectionId> collections,
                                                  const ExportRequest& request) const {
  return core::stitchCollections(snapshot(), sources_, collections, request);
}

// Scan the requested source, then scan any sources extracted from it. This lets an
// archive or container produce bytes that normal format modules can parse.
void Session::scanSourceAndDerived(SourceId id) {
  const size_t assetsBefore = state_->assets().size();
  const size_t diagnosticsBefore = state_->diagnostics().size();
  std::vector<SourceId> queue{id};

  for (size_t index = 0; index < queue.size(); ++index) {
    const auto current = queue[index];
    if (!scannedSources_.insert(current).second) {
      continue;
    }
    try {
      const auto& source = sources_.source(current);
      const auto resolved = source.memberPath ? resolveSource(*source.memberPath, source.parent) : source;
      if (resolved.id == current || scannedSources_.insert(resolved.id).second) {
        scanOneSource(resolved, queue);
      }
    } catch (const std::exception& ex) {
      state_->addError(ex.what(), sources_.reader(current).range(0, sources_.source(current).size));
    } catch (...) {
      state_->addError("Unknown exception", sources_.reader(current).range(0, sources_.source(current).size));
    }
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
void Session::scanOneSource(const SourceFile& source, std::vector<SourceId>& queue) {
  const auto id = source.id;
  bool acceptsKnownFormat = false;
  const auto addValidationFailure = [&](std::string_view processor, std::string_view operation,
                                        ValidationReport validation) {
    auto diagnostics = validation.takeDiagnostics();
    for (auto& diagnostic : diagnostics) {
      diagnostic.message = std::string(processor) + " " + std::string(operation) + " failed: " + diagnostic.message;
      if (!diagnostic.range.valid() || !sources_.contains(diagnostic.range.source)) {
        diagnostic.range = SourceRange{.source = source.id, .offset = 0, .size = source.size};
      }
    }
    state_->addDiagnostics(std::move(diagnostics));
  };

  // Extraction is a distinct first stage. Producing valid children means the
  // extractor has consumed the input, so ordinary scanners only see the children.
  for (const auto& extractor : extractors_) {
    if (!accepts(source, extractor.acceptedFormats)) {
      continue;
    }
    acceptsKnownFormat = true;
    try {
      ExtractionResult result = extractor.extract(ExtractionInput{
          .source = source,
          .reader = sources_.reader(id),
          .sources = sources_,
      });
      prepareDiagnosticRanges(result.diagnostics, source);
      auto validation = validateExtractionResult(source.id, result, sources_);
      if (!validation.empty()) {
        addValidationFailure(extractor.name, "extraction", std::move(validation));
        continue;
      }

      state_->addDiagnostics(std::move(result.diagnostics));
      if (!result.sources.empty()) {
        for (auto& extracted : result.sources) {
          queue.push_back(sources_.addDerived(std::move(extracted.file), std::move(extracted.bytes), source.id));
        }
        return;
      }
    } catch (const std::exception& ex) {
      state_->addError(std::string(extractor.name) + " extraction failed: " + ex.what(),
                       SourceRange{.source = source.id, .offset = 0, .size = source.size});
    } catch (...) {
      state_->addError(extractor.name + " extraction failed: Unknown exception",
                       SourceRange{.source = source.id, .offset = 0, .size = source.size});
    }
  }

  std::vector<FormatScan> scans;
  scans.reserve(formats_.size());
  for (const auto& module : formats_) {
    if (!accepts(source, module.acceptedFormats)) {
      continue;
    }
    acceptsKnownFormat = true;
    scans.push_back(FormatScan{.format = module});
  }

  const ScanInput input{
      .source = source,
      .reader = sources_.reader(id),
      .ids = state_->scanIds(),
      .retained = RetainedSource{id, sources_.sharedBytes(id)},
  };
  runFormatScans(scans, input);

  // Admission stays serial and follows registry order. Scanner callbacks only
  // read the shared source bytes and build their private result values.
  for (auto& scan : scans) {
    const FormatModule& module = scan.format;
    for (auto& asset : scan.result.assets) {
      if (auto* bank = std::get_if<SoundBankAsset>(&asset)) {
        bank->localSamples.preferredFilter = module.preferredSampleFilter;
      } else if (auto* samples = std::get_if<SamplePoolAsset>(&asset)) {
        samples->pool.preferredFilter = module.preferredSampleFilter;
      } else if (const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
                 sequence != nullptr && !sequence->program.runtime.valid()) {
        scan.result.diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .message = "Sequence program has no runtime executor",
            .range = sequence->metadata.range,
        });
      }
    }
    prepareDiagnosticRanges(scan.result.diagnostics, source);
    auto validation = validateScanResult(source.id, scan.result, sources_, state_->assets());
    if (!validation.empty()) {
      addValidationFailure(module.name, "scan", std::move(validation));
      continue;
    }
    state_->appendScan(std::move(scan.result));
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

void Session::removeSourceFamily(SourceId source, std::vector<SourceId>& removedSources) {
  auto family = sources_.removeFamily(source);
  for (const SourceId removed : family) {
    scannedSources_.erase(removed);
  }
  removedSources.insert(removedSources.end(), family.begin(), family.end());
}

void Session::rebuildCollections() {
  state_->reconcileCollections(dependencyCollections(AssetCatalog{sources_, state_->assets()}));
}

}  // namespace vgmtrans::core

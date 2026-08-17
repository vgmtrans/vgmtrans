/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"
#include "value/synth/SampleFiltering.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// Lightweight read-only view used while rebuilding collections. It borrows the
// source store and cheaply shares the Session's immutable asset and fact views
// instead of materializing a public SessionSnapshot.
class MatchContext {
public:
  MatchContext(const SourceStore& sources, SharedSequence<Asset> assets, SharedSequence<MatchFact> matchFacts)
      : sources_(sources), assets_(std::move(assets)), matchFacts_(std::move(matchFacts)) {}

  [[nodiscard]] const SourceStore& sources() const noexcept { return sources_; }
  [[nodiscard]] const SharedSequence<Asset>& assets() const noexcept { return assets_; }
  [[nodiscard]] const SharedSequence<MatchFact>& matchFacts() const noexcept { return matchFacts_; }

private:
  const SourceStore& sources_;
  SharedSequence<Asset> assets_;
  SharedSequence<MatchFact> matchFacts_;
};

// Formats bind collection-local meaning into private instrument copies and an
// optional compatible runtime. Membership was fixed earlier by matching, so a
// binder can edit asset contents but cannot add, remove, or reorder members.
class CollectionBindingContext {
public:
  CollectionBindingContext(const SequenceProgramAsset* sequence, SequenceRuntime& sequenceRuntime,
                           std::span<InstrumentSetAsset> instrumentSets,
                           std::span<const SampleCollectionAsset* const> sampleCollections,
                           std::vector<Diagnostic>& diagnostics)
      : sequence_(sequence), sequenceRuntime_(sequenceRuntime), instrumentSets_(instrumentSets),
        sampleCollections_(sampleCollections), diagnostics_(diagnostics) {}

  [[nodiscard]] const SequenceProgramAsset* sequence() const noexcept { return sequence_; }
  [[nodiscard]] std::span<InstrumentSetAsset> instrumentSets() const noexcept { return instrumentSets_; }
  [[nodiscard]] std::span<const SampleCollectionAsset* const> sampleCollections() const noexcept {
    return sampleCollections_;
  }

  [[nodiscard]] InstrumentSetAsset* instrumentSet(AssetId id) const noexcept {
    const auto found =
        std::ranges::find(instrumentSets_, id, [](const InstrumentSetAsset& asset) { return asset.metadata.id; });
    return found == instrumentSets_.end() ? nullptr : &*found;
  }

  // Stored command closures require the original compiler cursor's Playback
  // type. Runtime configuration may change, but that command-facing ABI may not.
  void replaceSequenceRuntime(SequenceRuntime replacement) {
    if (sequence_ == nullptr) {
      fail("Cannot replace the runtime of a collection without a sequence");
      return;
    }
    if (!replacement.valid()) {
      fail("Collection binder supplied an invalid sequence runtime", sequence_->metadata.range);
      return;
    }
    const void* expected = sequence_->program.runtime.commandPlaybackType;
    if (expected == nullptr || replacement.commandPlaybackType != expected) {
      fail("Collection binder supplied an incompatible sequence runtime", sequence_->metadata.range);
      return;
    }
    sequenceRuntime_ = std::move(replacement);
  }

  void warning(std::string message, SourceRange range = {}) {
    diagnostics_.push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = std::move(message),
        .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
    });
  }

  void fail(std::string message, SourceRange range = {}) {
    failed_ = true;
    diagnostics_.push_back(Diagnostic{
        .severity = Severity::Error,
        .message = std::move(message),
        .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
    });
  }

  [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
  const SequenceProgramAsset* sequence_ = nullptr;
  SequenceRuntime& sequenceRuntime_;
  std::span<InstrumentSetAsset> instrumentSets_;
  std::span<const SampleCollectionAsset* const> sampleCollections_;
  std::vector<Diagnostic>& diagnostics_;
  bool failed_ = false;
};

struct FormatModule {
  // Function table registered by one format. Recognition belongs at the start
  // of scan(), which returns an empty result when the source does not match.
  using Scan = std::function<ScanResult(const ScanInput& input)>;
  using ResolveCollections = std::function<std::vector<DesiredCollection>(const MatchContext& context)>;
  using BindCollection = std::function<void(CollectionBindingContext& context)>;

  std::string name;
  // Used when a request delegates sample filtering to the owning format.
  SampleFilter preferredSampleFilter = SampleFilter::None;
  // Known source representations accepted by this module. Sources without a
  // known format are still offered to every module for normal discovery.
  std::vector<std::string> acceptedFormats;
  Scan scan;
  // Defaults to name when empty. Set this when a resolver intentionally uses a
  // different key prefix for its collections.
  std::string collectionResolverId;
  ResolveCollections resolveCollections;
  BindCollection bindCollection;

  [[nodiscard]] std::string_view collectionResolver() const noexcept {
    return collectionResolverId.empty() ? std::string_view(name) : std::string_view(collectionResolverId);
  }
};

}  // namespace vgmtrans::core

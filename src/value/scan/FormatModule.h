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

// Formats bind collection-local meaning into private instrument copies and a
// copied sequence runtime. Member lists themselves were fixed during matching.
struct CollectionBindingContext {
public:
  CollectionBindingContext(const SequenceProgramAsset* sequence, SequenceRuntime& sequenceRuntime,
                           std::span<SoundBankAsset> soundBanks, std::span<const SamplePoolAsset* const> samplePools,
                           std::vector<Diagnostic>& diagnostics)
      : sequence(sequence), soundBanks(soundBanks), samplePools(samplePools), diagnostics(diagnostics),
        sequenceRuntime_(sequenceRuntime) {}

  const SequenceProgramAsset* sequence;
  std::span<SoundBankAsset> soundBanks;
  std::span<const SamplePoolAsset* const> samplePools;
  std::vector<Diagnostic>& diagnostics;
  bool failed = false;

  [[nodiscard]] SoundBankAsset* soundBank(AssetId id) const noexcept {
    const auto found = std::ranges::find(soundBanks, id, [](const SoundBankAsset& asset) { return asset.metadata.id; });
    return found == soundBanks.end() ? nullptr : &*found;
  }

  [[nodiscard]] bool replaceSequenceRuntime(SequenceRuntime replacement) {
    const SourceRange range = sequence != nullptr ? sequence->metadata.range : SourceRange{};
    if (!sequenceRuntime_.valid()) {
      fail("Collection binding cannot replace a sequence runtime with no executor", range);
      return false;
    }
    if (!replacement.valid()) {
      fail("Collection binding produced a replacement sequence runtime with no executor", range);
      return false;
    }
    if (sequenceRuntime_.family == nullptr || replacement.family == nullptr ||
        sequenceRuntime_.family != replacement.family) {
      fail("Collection binding produced an incompatible sequence runtime family", range);
      return false;
    }
    sequenceRuntime_ = std::move(replacement);
    return true;
  }

  void warning(std::string message, SourceRange range = {}) { report(Severity::Warning, std::move(message), range); }

  void fail(std::string message, SourceRange range = {}) {
    failed = true;
    report(Severity::Error, std::move(message), range);
  }

private:
  void report(Severity severity, std::string message, SourceRange range) {
    diagnostics.push_back(Diagnostic{
        .severity = severity,
        .message = std::move(message),
        .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
    });
  }

  SequenceRuntime& sequenceRuntime_;
};

struct FormatModule {
  // Function table registered by one format. Recognition belongs at the start
  // of scan(), which returns an empty result when the source does not match.
  using Scan = std::function<ScanResult(const ScanInput& input)>;
  using ResolveCollections = std::function<std::vector<DesiredCollection>(const MatchContext& context)>;

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
  // Session resolution installs this closure directly on matching collections.
  CollectionBinder bindCollection;

  [[nodiscard]] std::string_view collectionResolver() const noexcept {
    return collectionResolverId.empty() ? std::string_view(name) : std::string_view(collectionResolverId);
  }
};

}  // namespace vgmtrans::core

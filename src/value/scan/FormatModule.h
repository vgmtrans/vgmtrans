/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"
#include "value/synth/SampleFiltering.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::core {

struct PerformanceSequence;

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

struct CollectionPrepareContext {
  const SourceStore& sources;
  const SessionSnapshot& snapshot;
  const Collection& collection;
};

// A format can adjust sequence playback using other assets in the collection.
// This runs after sequence rendering and before modulation analysis or export.
using FinalizeCollectionPerformance = std::function<void(PerformanceSequence&)>;

struct PreparedCollectionAssets {
  // If set, these replace the collection's original instrument sets. If not
  // set, the originals are kept. An empty vector removes all instrument sets.
  std::optional<std::vector<InstrumentSetAsset>> replacementInstrumentSets;
  FinalizeCollectionPerformance finalizePerformance;
  std::vector<Diagnostic> diagnostics;
};

struct FormatModule {
  // Function table registered by one format. Recognition belongs at the start
  // of scan(), which returns an empty result when the source does not match.
  using Scan = std::function<ScanResult(const ScanInput& input)>;
  using ResolveCollections = std::function<std::vector<DesiredCollection>(const MatchContext& context)>;
  using PrepareCollection = std::function<PreparedCollectionAssets(const CollectionPrepareContext& context)>;

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
  PrepareCollection prepareCollection;
};

}  // namespace vgmtrans::core

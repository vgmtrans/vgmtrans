/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"

#include <functional>
#include <span>
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

// Some sequence semantics depend on assets selected by a collection. Formats
// may enrich the transient rendered performance here, after VM execution and
// before modulation analysis or target-specific export.
using FinalizeCollectionPerformance = std::function<void(PerformanceSequence&)>;

struct PreparedCollectionAssets {
  // When a format prepares a collection, these are the complete instrument
  // sets to use for that collection, replacing its durable scanned sets.
  std::vector<InstrumentSetAsset> replacementInstrumentSets;
  FinalizeCollectionPerformance finalizePerformance;
  std::vector<Diagnostic> diagnostics;
};

struct FormatModule {
  // Function table registered by one format. New modules should put recognition
  // at the start of scan() and return an empty result when the source does not
  // match. canScan remains only as a migration adapter for older modules.
  using CanScan = std::function<bool(const SourceFile& source, std::span<const u8> bytes)>;
  using Scan = std::function<ScanResult(const ScanInput& input)>;
  using ResolveCollections = std::function<std::vector<DesiredCollection>(const MatchContext& context)>;
  using PrepareCollection = std::function<PreparedCollectionAssets(const CollectionPrepareContext& context)>;

  std::string name;
  // Transitional prefilter. It may be null; duplicating layout discovery here
  // defeats the parse-once model and should not be done by new modules.
  CanScan canScan;
  Scan scan;
  // Defaults to name when empty. Set this when a resolver intentionally uses a
  // different key prefix for its collections.
  std::string collectionResolverId;
  ResolveCollections resolveCollections;
  PrepareCollection prepareCollection;
};

}  // namespace vgmtrans::core

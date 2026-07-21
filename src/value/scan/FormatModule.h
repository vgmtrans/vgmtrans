/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

struct MatchContext {
  const SourceStore& sources;
  const SessionSnapshot& snapshot;
};

struct MaterializedAsset {
  std::string slot;
  Asset asset;
};

struct MaterializationContext {
  const SourceStore& sources;
  const SessionSnapshot& snapshot;
  const DesiredCollection& collection;
  ScanIdAllocator& ids;
  std::function<AssetId(std::string_view)> assetIdForSlot;

  [[nodiscard]] const SequenceProgramAsset* sequenceAsset() const {
    return collection.sequence ? snapshot.asset<SequenceProgramAsset>(*collection.sequence) : nullptr;
  }

  [[nodiscard]] std::vector<const InstrumentSetAsset*> selectedInstrumentSets() const {
    std::vector<const InstrumentSetAsset*> assets;
    assets.reserve(collection.instrumentSets.size());
    for (const AssetId id : collection.instrumentSets) {
      if (const auto* asset = snapshot.asset<InstrumentSetAsset>(id)) {
        assets.push_back(asset);
      }
    }
    return assets;
  }

  [[nodiscard]] std::vector<const SampleCollectionAsset*> selectedSampleCollections() const {
    std::vector<const SampleCollectionAsset*> assets;
    assets.reserve(collection.sampleCollections.size());
    for (const AssetId id : collection.sampleCollections) {
      if (const auto* asset = snapshot.asset<SampleCollectionAsset>(id)) {
        assets.push_back(asset);
      }
    }
    return assets;
  }

  [[nodiscard]] std::vector<const MiscAsset*> selectedMiscAssets() const {
    std::vector<const MiscAsset*> assets;
    assets.reserve(collection.miscAssets.size());
    for (const AssetId id : collection.miscAssets) {
      if (const auto* asset = snapshot.asset<MiscAsset>(id)) {
        assets.push_back(asset);
      }
    }
    return assets;
  }

  [[nodiscard]] std::optional<ScanInput> inputFor(SourceRange range) const {
    if (!range.valid() || !sources.contains(range.source)) {
      return std::nullopt;
    }
    return ScanInput{
        .source = sources.source(range.source),
        .reader = sources.reader(range.source),
        .ids = ids,
    };
  }
};

struct MaterializationResult {
  DesiredCollection collection;
  std::vector<MaterializedAsset> assets;
  SourceMap sourceMap;
  std::vector<Diagnostic> diagnostics;
};

struct FormatModule {
  // Function table registered by one format. New modules should put recognition
  // at the start of scan() and return an empty result when the source does not
  // match. canScan remains only as a migration adapter for older modules.
  using CanScan = bool (*)(const SourceFile& source, std::span<const u8> bytes);
  using Scan = ScanResult (*)(const ScanInput& input);
  using ResolveCollections = std::vector<DesiredCollection> (*)(const MatchContext& context);
  using MaterializeCollection = MaterializationResult (*)(const MaterializationContext& context);

  std::string name;
  // Transitional prefilter. It may be null; duplicating layout discovery here
  // defeats the parse-once model and should not be done by new modules.
  CanScan canScan = nullptr;
  Scan scan = nullptr;
  // Defaults to name when empty. Set this when a resolver intentionally uses a
  // different key prefix for its collections.
  std::string collectionResolverId;
  ResolveCollections resolveCollections = nullptr;
  MaterializeCollection materializeCollection = nullptr;
};

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// One format-owned value joined to its asset at the heterogeneous asset
// boundary. This borrowed view is valid only during discovery; durable binders
// capture stable IDs or owned values instead.
template <class AssetT, class DataT>
struct AssetWithData {
  const AssetT* asset = nullptr;
  const DataT* data = nullptr;
  const SourceFile* source = nullptr;

  [[nodiscard]] AssetId id() const noexcept { return asset->metadata.id; }
  [[nodiscard]] std::optional<SourceId> sourceId() const noexcept {
    return asset->metadata.range.valid() ? std::optional{asset->metadata.range.source} : std::nullopt;
  }
};

// Session-wide, read-only input to format-owned collection discovery. It owns a
// cheap shared asset view so pointers returned from a temporary context remain
// valid for the context's lifetime.
class CollectionDiscoveryContext {
public:
  CollectionDiscoveryContext(const SourceStore& sources, SharedSequence<Asset> assets);

  [[nodiscard]] const SourceFile* sourceFor(const AssetMetadata& metadata) const noexcept;
  [[nodiscard]] const Asset* asset(AssetId id) const noexcept;

  template <class AssetT>
  [[nodiscard]] const AssetT* asset(AssetId id) const noexcept {
    const auto* found = asset(id);
    return found != nullptr ? std::get_if<AssetT>(found) : nullptr;
  }

  template <class AssetT>
  [[nodiscard]] std::vector<const AssetT*> assets(std::string_view format = {}) const {
    std::vector<const AssetT*> matches;
    for (const auto& value : assets_) {
      const auto* typed = std::get_if<AssetT>(&value);
      if (typed != nullptr && (format.empty() || typed->metadata.format == format)) {
        matches.push_back(typed);
      }
    }
    std::ranges::sort(matches, {}, [](const AssetT* value) { return value->metadata.id.value; });
    return matches;
  }

  template <class AssetT, class DataT>
  [[nodiscard]] std::vector<AssetWithData<AssetT, DataT>> assetsWithData() const {
    std::vector<AssetWithData<AssetT, DataT>> matches;
    for (const AssetT* value : assets<AssetT>()) {
      if (const auto* data = value->privateData.template get<DataT>()) {
        matches.push_back(AssetWithData<AssetT, DataT>{
            .asset = value,
            .data = data,
            .source = sourceFor(value->metadata),
        });
      }
    }
    return matches;
  }

private:
  const SourceStore& sources_;
  SharedSequence<Asset> assets_;
  std::unordered_map<u32, const Asset*> assetsById_;
};

// Small mutable helper for building one DesiredCollection deterministically.
// It owns duplicate suppression and common missing-role issues, while the
// resolver remains responsible for format-specific matching policy.
class CollectionAssembly {
public:
  CollectionAssembly(std::string localKey, std::string name);

  CollectionAssembly& sequence(AssetId id);
  CollectionAssembly& soundBank(AssetId id);
  CollectionAssembly& samplePool(AssetId id);
  CollectionAssembly& misc(AssetId id);
  CollectionAssembly& incomplete(CollectionIssue issue);
  // The binder outlives discovery. Capture only stable IDs and owned values,
  // then recover current collection members through CollectionBindingContext.
  CollectionAssembly& bind(CollectionBinder binder);
  CollectionAssembly& ambiguous(std::string message, std::optional<AssetId> asset = std::nullopt,
                                std::optional<SourceRange> range = std::nullopt);
  CollectionAssembly& requireSoundBank();

  [[nodiscard]] DesiredCollection finish() &&;

private:
  void addUnique(std::vector<AssetId>& ids, AssetId id);

  DesiredCollection collection_;
};

}  // namespace vgmtrans::core

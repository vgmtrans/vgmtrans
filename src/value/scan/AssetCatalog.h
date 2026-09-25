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

// Negative scores reject a candidate; zero is a valid fallback. Retain every
// highest-scoring candidate in input order so formats can report ambiguity.
// Returned pointers borrow the caller's candidate vector.
template <class Candidate, class Score>
[[nodiscard]] std::vector<const Candidate*> bestMatches(const std::vector<Candidate>& candidates, Score score) {
  std::vector<const Candidate*> selected;
  int best = 0;
  for (const auto& candidate : candidates) {
    const int rank = score(candidate);
    if (rank < best) {
      continue;
    }
    if (rank > best) {
      best = rank;
      selected.clear();
    }
    selected.push_back(&candidate);
  }
  return selected;
}

template <class Candidate, class Score>
std::vector<const Candidate*> bestMatches(const std::vector<Candidate>&&, Score) = delete;

// One format-owned value joined to its asset at the heterogeneous asset
// boundary. This borrowed view is valid only during selection; recipes and
// resolved dependencies retain stable IDs and owned values instead.
template <class AssetT, class DataT>
struct AssetWithData {
  const AssetT* asset = nullptr;
  const DataT* data = nullptr;
  const SourceFile* source = nullptr;

  [[nodiscard]] AssetId id() const noexcept { return asset->metadata.id; }
  [[nodiscard]] SourceId sourceId() const noexcept { return asset->metadata.range.source; }
};

// Session-wide, read-only input to format-owned collection discovery. It owns a
// cheap shared asset view so pointers returned from a temporary context remain
// valid for the context's lifetime.
class AssetCatalog {
public:
  AssetCatalog(const SourceStore& sources, SharedSequence<Asset> assets)
      : AssetCatalog(sources.sourceFiles(), std::move(assets)) {}
  AssetCatalog(std::vector<SourceFile> sources, SharedSequence<Asset> assets);

  [[nodiscard]] SourceId sourceRoot(SourceId source) const noexcept;
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
  std::vector<SourceFile> sources_;
  std::unordered_map<u32, size_t> sourcesById_;
  SharedSequence<Asset> assets_;
  std::unordered_map<u32, const Asset*> assetsById_;
};

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/AssetCatalog.h"

#include <span>
#include <stdexcept>

namespace vgmtrans::core {

template <class T>
[[nodiscard]] AssetId dependencyId(const T& value) {
  if constexpr (requires { value.id(); }) {
    return value.id();
  } else {
    return value.metadata.id;
  }
}

template <class T>
[[nodiscard]] AssetId dependencyId(const T* value) {
  return dependencyId(*value);
}

enum class ResolutionMode { Automatic, Manual };

// One asset's request, with candidates restricted to the user's selection for
// manual collections. Selectors never mutate assets or construct collections.
class DependencyContext {
public:
  DependencyContext(const AssetCatalog& assets, const Asset& owner, ResolutionMode mode,
                    std::span<const AssetId> candidates = {})
      : assets_(assets), owner_(owner), mode_(mode), candidates_(candidates) {}

  [[nodiscard]] const AssetMetadata& metadata() const { return core::metadata(owner_); }
  [[nodiscard]] const SourceFile* source() const { return assets_.sourceFor(metadata()); }
  [[nodiscard]] bool manual() const { return mode_ == ResolutionMode::Manual; }
  [[nodiscard]] const AssetCatalog& catalog() const { return assets_; }

  template <class Data>
  [[nodiscard]] const Data& data() const {
    const auto* value = std::visit([](const auto& asset) { return asset.privateData.template get<Data>(); }, owner_);
    if (value == nullptr) {
      throw std::logic_error("Asset is missing its dependency data");
    }
    return *value;
  }

  template <class AssetT>
  [[nodiscard]] std::vector<const AssetT*> candidates(std::string_view format = {}) const {
    auto values = assets_.assets<AssetT>(format);
    std::erase_if(values, [&](const AssetT* value) { return !allowed(value->metadata.id); });
    if (manual()) {
      std::ranges::stable_sort(values, [&](const auto& left, const auto& right) {
        return std::ranges::find(candidates_, dependencyId(left)) < std::ranges::find(candidates_, dependencyId(right));
      });
    }
    return values;
  }

  template <class AssetT, class Data>
  [[nodiscard]] std::vector<AssetWithData<AssetT, Data>> candidates() const {
    auto values = assets_.assetsWithData<AssetT, Data>();
    std::erase_if(values, [&](const auto& value) { return !allowed(value.id()); });
    if (manual()) {
      std::ranges::stable_sort(values, [&](const auto& left, const auto& right) {
        return std::ranges::find(candidates_, dependencyId(left)) < std::ranges::find(candidates_, dependencyId(right));
      });
    }
    return values;
  }

private:
  [[nodiscard]] bool allowed(AssetId id) const {
    if (manual()) {
      return std::ranges::find(candidates_, id) != candidates_.end();
    }
    const auto* asset = assets_.asset(id);
    const auto* ownerSource = source();
    const auto* providerSource = asset != nullptr ? assets_.sourceFor(core::metadata(*asset)) : nullptr;
    if (ownerSource != nullptr && providerSource != nullptr && (ownerSource->derived() || providerSource->derived())) {
      return assets_.sourceRoot(ownerSource->id) == assets_.sourceRoot(providerSource->id);
    }
    return true;
  }

  const AssetCatalog& assets_;
  const Asset& owner_;
  ResolutionMode mode_;
  std::span<const AssetId> candidates_;
};

template <class Range>
[[nodiscard]] DependencySelection selectAll(const Range& candidates) {
  DependencySelection result;
  for (const auto& candidate : candidates) {
    result.add(dependencyId(candidate));
  }
  return result;
}

// An intentional group and an unresolved choice are different values. A tie
// retains alternatives for inspection without arbitrarily selecting a provider.
template <class Range>
[[nodiscard]] DependencySelection selectOne(const Range& candidates) {
  if (candidates.size() <= 1) {
    return selectAll(candidates);
  }
  DependencySelection result;
  for (const auto& candidate : candidates) {
    result.alternatives.push_back(dependencyId(candidate));
  }
  return result;
}

[[nodiscard]] const AssetRecipe* assetRecipe(const Asset& asset);
void resolveDependencies(const AssetCatalog& assets, DesiredCollection& collection,
                         ResolutionMode mode = ResolutionMode::Automatic);
[[nodiscard]] std::vector<std::pair<std::string, DesiredCollection>> dependencyCollections(
    const AssetCatalog& assets, std::span<const AssetId> explicitRoots = {});

}  // namespace vgmtrans::core

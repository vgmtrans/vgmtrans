/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"

#include <variant>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Owns scanned assets and remembers which source scan produced each one. Source
// ranges are still checked during removal so malformed or hand-built assets cannot
// survive just because their ownership map entry is missing.
class AssetStore {
public:
  [[nodiscard]] const std::vector<Asset>& all() const noexcept { return assets_; }
  [[nodiscard]] bool contains(AssetId id) const noexcept;
  [[nodiscard]] const Asset* find(AssetId id) const noexcept;

  template <typename T>
  [[nodiscard]] const T* findAs(AssetId id) const noexcept {
    const auto* asset = find(id);
    return asset != nullptr ? std::get_if<T>(asset) : nullptr;
  }

  void append(std::vector<Asset> assets, SourceId owner);
  [[nodiscard]] std::unordered_set<u32> removeForSources(const std::vector<SourceId>& sources);

private:
  void rebuildIndex();

  std::vector<Asset> assets_;
  std::unordered_map<u32, size_t> assetsById_;
  std::unordered_map<u32, SourceId> sourceOwners_;
};

}  // namespace vgmtrans::core

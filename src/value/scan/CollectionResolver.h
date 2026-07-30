/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// All matching facts for one asset, joined once. Format resolvers can ask the
// resulting value for common facts without rebuilding maps keyed by AssetId.
template <class AssetT>
struct AssetFacts {
  const AssetT* assetValue = nullptr;
  std::optional<SourceId> sourceId;
  const SourceFile* source = nullptr;
  std::vector<const MatchFact*> facts;

  [[nodiscard]] const AssetT& asset() const { return *assetValue; }

  [[nodiscard]] std::optional<u32> id(std::string_view domain) const {
    for (const MatchFact* fact : facts) {
      if (const auto* value = std::get_if<IdMatchFact>(&fact->payload); value != nullptr && value->domain == domain) {
        return value->value;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<AssetId> relation(std::string_view domain) const {
    for (const MatchFact* fact : facts) {
      if (const auto* value = std::get_if<AssetRelationFact>(&fact->payload);
          value != nullptr && value->domain == domain) {
        return value->target;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<SampleCoverageFact> coverage(std::string_view domain) const {
    for (const MatchFact* fact : facts) {
      if (const auto* value = std::get_if<SampleCoverageFact>(&fact->payload);
          value != nullptr && value->domain == domain) {
        return *value;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::vector<u32> requirements(std::string_view domain) const {
    std::vector<u32> values;
    for (const MatchFact* fact : facts) {
      if (const auto* requirement = std::get_if<SampleRequirementFact>(&fact->payload);
          requirement != nullptr && requirement->domain == domain) {
        values.insert(values.end(), requirement->required.begin(), requirement->required.end());
      }
    }
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
    return values;
  }
};

// Read-only index over the accumulated match facts. Resolvers stay pure, but this
// avoids hand-written variant/type/source lookups in every format-specific resolver.
class MatchFactIndex {
public:
  explicit MatchFactIndex(const MatchContext& context);

  [[nodiscard]] const SourceFile* sourceFor(const MatchFact& fact) const;
  [[nodiscard]] const Asset* asset(AssetId id) const noexcept;

  template <class AssetT>
  [[nodiscard]] const AssetT* asset(AssetId id) const noexcept {
    const auto* found = asset(id);
    return found != nullptr ? std::get_if<AssetT>(found) : nullptr;
  }

  template <class AssetT>
  [[nodiscard]] std::vector<AssetFacts<AssetT>> assets(std::string_view format) const {
    std::vector<AssetFacts<AssetT>> matches;
    for (const auto& fact : context_.matchFacts()) {
      if (!format.empty() && fact.format != format) {
        continue;
      }
      const auto* asset = this->template asset<AssetT>(fact.asset);
      if (asset == nullptr) {
        continue;
      }
      auto found = std::ranges::find_if(
          matches, [&](const AssetFacts<AssetT>& entry) { return entry.asset().metadata.id == fact.asset; });
      if (found == matches.end()) {
        matches.push_back(AssetFacts<AssetT>{
            .assetValue = asset,
            .sourceId = fact.scope.source,
            .source = sourceFor(fact),
            .facts = {&fact},
        });
      } else {
        found->facts.push_back(&fact);
        if (!found->sourceId && fact.scope.source) {
          found->sourceId = fact.scope.source;
          found->source = sourceFor(fact);
        }
      }
    }
    std::ranges::sort(matches, {}, [](const AssetFacts<AssetT>& entry) { return entry.asset().metadata.id.value; });
    return matches;
  }

private:
  // MatchContext is a small shared read view. Store the view itself so an index
  // constructed from a temporary MatchContext does not retain a dangling
  // reference to that wrapper.
  MatchContext context_;
  std::unordered_map<u32, const Asset*> assetsById_;
};

// Small mutable helper for building one DesiredCollection deterministically.
// It owns duplicate suppression and common missing-role status/issue handling,
// while the resolver remains responsible for format-specific matching policy.
class CollectionAssembly {
public:
  CollectionAssembly(CollectionKey key, std::string name);

  CollectionAssembly& sequence(AssetId id);
  CollectionAssembly& instrumentSet(AssetId id);
  CollectionAssembly& sampleCollection(AssetId id);
  CollectionAssembly& misc(AssetId id);
  CollectionAssembly& issue(CollectionIssue issue);
  CollectionAssembly& incomplete(CollectionIssue issue);
  CollectionAssembly& ambiguous(std::string message, std::optional<AssetId> asset = std::nullopt,
                                std::optional<SourceRange> range = std::nullopt);
  CollectionAssembly& requireSequence();
  CollectionAssembly& requireInstrumentSet();
  CollectionAssembly& requireSampleCollection();

  [[nodiscard]] DesiredCollection finish() &&;

private:
  void addUnique(std::vector<AssetId>& ids, AssetId id);

  DesiredCollection collection_;
};

}  // namespace vgmtrans::core

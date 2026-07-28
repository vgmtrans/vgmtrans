/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// Resolver for the common case where scanners emit CollectionMemberFact records.
// Formats with looser matching rules can provide their own resolver instead.
[[nodiscard]] std::vector<DesiredCollection> resolveCollectionMemberFacts(const MatchContext& context,
                                                                          std::string_view resolver,
                                                                          std::string_view format = {});

struct MatchFieldValue {
  std::string_view value;
};

[[nodiscard]] std::optional<MatchFieldValue> fieldValue(const FormatSpecificFact& fact, std::string_view name);
[[nodiscard]] std::optional<u32> fieldU32(const FormatSpecificFact& fact, std::string_view name);
[[nodiscard]] std::optional<s32> fieldS32(const FormatSpecificFact& fact, std::string_view name);
[[nodiscard]] bool fieldBool(const FormatSpecificFact& fact, std::string_view name, bool fallback = false);
[[nodiscard]] FormatSpecificFact formatFact(std::string kind, std::initializer_list<MatchField> fields);

template <class AssetT, class PayloadT>
struct AssetMatchView {
  const MatchFact& fact;
  const AssetT& asset;
  const PayloadT& payload;
  const SourceFile* source = nullptr;
};

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

  [[nodiscard]] std::optional<u64> offset() const {
    for (const MatchFact* fact : facts) {
      if (const auto* value = std::get_if<OffsetOrderFact>(&fact->payload)) {
        return value->offset;
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

  template <class AssetT, class PayloadT, class Predicate>
  [[nodiscard]] std::vector<AssetMatchView<AssetT, PayloadT>> facts(std::string_view format,
                                                                    Predicate predicate) const {
    std::vector<AssetMatchView<AssetT, PayloadT>> matches;
    for (const auto& fact : context_.matchFacts()) {
      if (!format.empty() && fact.format != format) {
        continue;
      }
      const auto* payload = std::get_if<PayloadT>(&fact.payload);
      if (payload == nullptr || !predicate(fact, *payload)) {
        continue;
      }
      const auto* asset = this->template asset<AssetT>(fact.asset);
      if (asset == nullptr) {
        continue;
      }
      matches.push_back(AssetMatchView<AssetT, PayloadT>{
          .fact = fact,
          .asset = *asset,
          .payload = *payload,
          .source = sourceFor(fact),
      });
    }
    return matches;
  }

  template <class AssetT>
  [[nodiscard]] std::vector<AssetMatchView<AssetT, FormatSpecificFact>> formatFacts(std::string_view format,
                                                                                    std::string_view kind) const {
    return facts<AssetT, FormatSpecificFact>(
        format, [kind](const MatchFact&, const FormatSpecificFact& payload) { return payload.kind == kind; });
  }

  template <class AssetT>
  [[nodiscard]] std::vector<AssetMatchView<AssetT, IdMatchFact>> idFacts(std::string_view format,
                                                                         std::string_view domain) const {
    return facts<AssetT, IdMatchFact>(
        format, [domain](const MatchFact&, const IdMatchFact& payload) { return payload.domain == domain; });
  }

  template <class AssetT>
  [[nodiscard]] std::vector<AssetMatchView<AssetT, OffsetOrderFact>> offsetFacts(std::string_view format) const {
    return facts<AssetT, OffsetOrderFact>(format, [](const MatchFact&, const OffsetOrderFact&) { return true; });
  }

  template <class AssetT>
  [[nodiscard]] std::vector<AssetMatchView<AssetT, SampleCoverageFact>> sampleCoverageFacts(
      std::string_view format, std::string_view domain) const {
    return facts<AssetT, SampleCoverageFact>(
        format, [domain](const MatchFact&, const SampleCoverageFact& payload) { return payload.domain == domain; });
  }

  template <class AssetT>
  [[nodiscard]] std::vector<AssetMatchView<AssetT, SampleRequirementFact>> sampleRequirementFacts(
      std::string_view format, std::string_view domain) const {
    return facts<AssetT, SampleRequirementFact>(
        format, [domain](const MatchFact&, const SampleRequirementFact& payload) { return payload.domain == domain; });
  }

private:
  // MatchContext is a small shared read view. Store the view itself so an index
  // constructed from a temporary MatchContext does not retain a dangling
  // reference to that wrapper.
  MatchContext context_;
  std::unordered_map<u32, const Asset*> assetsById_;
};

struct SampleCoverageProvider {
  // index is an opaque caller-owned identity returned in the selection.
  std::size_t index = 0;
  std::optional<u32> groupId;
  u32 first = 0;
  u32 count = 0;
  // Later or otherwise preferred providers should use a larger priority.
  u64 priority = 0;
};

struct SampleCoverageSelection {
  std::vector<std::size_t> providers;
  std::vector<u32> missing;
  bool preferredGroupFound = false;
};

// Choose the preferred sample group, then add the few source-associated or
// coverage-contributing providers needed by the sequence. Missing and zero
// group ids intentionally compare equal for formats with anonymous sets.
[[nodiscard]] SampleCoverageSelection selectSampleCoverage(std::optional<u32> preferredGroup,
                                                           std::span<const u32> required,
                                                           std::span<const SampleCoverageProvider> providers);

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

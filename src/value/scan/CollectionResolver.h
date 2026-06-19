/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
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

// Read-only index over the accumulated match facts. Resolvers stay pure, but this
// avoids hand-written variant/type/source lookups in every format-specific resolver.
class MatchFactIndex {
public:
  explicit MatchFactIndex(const MatchContext& context);

  [[nodiscard]] const MatchContext& context() const noexcept { return context_; }
  [[nodiscard]] const SourceFile* sourceFor(const MatchFact& fact) const;

  template <class AssetT, class PayloadT, class Predicate>
  [[nodiscard]] std::vector<AssetMatchView<AssetT, PayloadT>> facts(std::string_view format,
                                                                    Predicate predicate) const {
    std::vector<AssetMatchView<AssetT, PayloadT>> matches;
    for (const auto& fact : context_.snapshot.matchFacts()) {
      if (!format.empty() && fact.format != format) {
        continue;
      }
      const auto* payload = std::get_if<PayloadT>(&fact.payload);
      if (payload == nullptr || !predicate(fact, *payload)) {
        continue;
      }
      const auto* asset = assetById<AssetT>(context_.snapshot, fact.asset);
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
  [[nodiscard]] std::vector<AssetMatchView<AssetT, FormatSpecificFact>>
  formatFacts(std::string_view format, std::string_view kind) const {
    return facts<AssetT, FormatSpecificFact>(format, [kind](const MatchFact&, const FormatSpecificFact& payload) {
      return payload.kind == kind;
    });
  }

private:
  const MatchContext& context_;
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

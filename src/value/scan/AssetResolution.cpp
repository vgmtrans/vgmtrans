/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/AssetResolution.h"

#include <unordered_set>

namespace vgmtrans::core {

const AssetRecipe* assetRecipe(const Asset& asset) {
  return std::visit(
      [](const auto& value) -> const AssetRecipe* {
        if constexpr (requires { value.recipe; }) {
          return &value.recipe;
        } else {
          return nullptr;
        }
      },
      asset);
}

namespace {

std::vector<AssetId>& membersFor(CollectionMembers& members, DependencyRole role) {
  switch (role) {
    case DependencyRole::SoundBank:
      return members.soundBanks;
    case DependencyRole::SamplePool:
      return members.samplePools;
    case DependencyRole::Misc:
      return members.miscAssets;
  }
  throw std::logic_error("Unknown dependency role");
}

bool matchesRole(const Asset& asset, DependencyRole role) {
  switch (role) {
    case DependencyRole::SoundBank:
      return std::holds_alternative<SoundBankAsset>(asset);
    case DependencyRole::SamplePool:
      return std::holds_alternative<SamplePoolAsset>(asset);
    case DependencyRole::Misc:
      return std::holds_alternative<MiscAsset>(asset);
  }
  return false;
}

class Resolver {
public:
  Resolver(const AssetCatalog& assets, DesiredCollection& result, ResolutionMode mode)
      : assets_(assets), result_(result), mode_(mode), selected_(result.members) {}

  void visit(AssetId id) {
    const auto* asset = assets_.asset(id);
    if (asset == nullptr) {
      return;
    }
    if (active_.contains(id.value)) {
      issue(id, "dependency-cycle", "Asset dependencies contain a cycle", Severity::Error);
      return;
    }
    if (!visited_.insert(id.value).second) {
      return;
    }
    const auto* recipe = assetRecipe(*asset);
    if (recipe == nullptr) {
      return;
    }
    active_.insert(id.value);
    std::unordered_set<DependencyRole> overriddenRoles;
    for (const auto& dependency : recipe->dependencies) {
      DependencySelection selection;
      // Manual bank choices override the sequence's inferred or direct banks.
      // Banks still resolve their own samples inside the selected pool list.
      const bool override = mode_ == ResolutionMode::Manual && std::holds_alternative<SequenceProgramAsset>(*asset);
      if (override && !overriddenRoles.insert(dependency.role).second) {
        continue;
      }
      try {
        if (override) {
          for (const auto selected : membersFor(selected_, dependency.role)) {
            selection.add(selected);
          }
        } else {
          selection =
              dependency.select(DependencyContext{assets_, *asset, mode_, membersFor(selected_, dependency.role)});
        }
      } catch (const std::exception& error) {
        issue(id, "dependency-resolution-failed", std::string("Asset dependency resolution failed: ") + error.what(),
              Severity::Error);
        continue;
      } catch (...) {
        issue(id, "dependency-resolution-failed", "Asset dependency resolution failed", Severity::Error);
        continue;
      }
      accept(*asset, dependency.role, std::move(selection));
    }
    if (const auto* bank = std::get_if<SoundBankAsset>(asset)) {
      DependencySelection concreteSamples;
      for (const auto& instrument : bank->instruments) {
        for (const auto& region : instrument.regions) {
          const auto owner = region.sample.owner();
          if (!region.sample.valid() || owner == id) {
            continue;
          }
          const auto alreadyLinked = [&](const DependencyTarget& target) { return target.asset == owner; };
          const bool declared = std::ranges::any_of(result_.dependencies, [&](const ResolvedDependency& dependency) {
            return dependency.owner == id && dependency.role == DependencyRole::SamplePool &&
                   std::ranges::any_of(dependency.targets, alreadyLinked);
          });
          if (!declared && !std::ranges::any_of(concreteSamples.targets, alreadyLinked)) {
            concreteSamples.add(owner);
          }
        }
      }
      if (!concreteSamples.targets.empty()) {
        accept(*asset, DependencyRole::SamplePool, std::move(concreteSamples));
      }
    }
    active_.erase(id.value);
  }

private:
  void accept(const Asset& asset, DependencyRole role, DependencySelection selection) {
    const auto id = metadata(asset).id;
    for (auto& diagnostic : selection.issues) {
      if (!diagnostic.asset) {
        diagnostic.asset = id;
      }
      if (!diagnostic.range.valid()) {
        diagnostic.range = metadata(asset).range;
      }
      result_.issues.push_back(std::move(diagnostic));
    }
    const auto explains = [&](CollectionIssueImpact impact) {
      return std::ranges::any_of(selection.issues, [impact](const auto& issue) { return issue.impact >= impact; });
    };
    if (!selection.alternatives.empty() && !explains(CollectionIssueImpact::Ambiguous)) {
      result_.issues.push_back(
          ambiguousMatchIssue("Asset dependency matches multiple providers", id, metadata(asset).range));
    } else if (selection.targets.empty() && selection.alternatives.empty() &&
               !explains(CollectionIssueImpact::Incomplete)) {
      auto missing = role == DependencyRole::SoundBank ? missingSoundBankIssue() : missingSamplePoolIssue();
      missing.asset = id;
      result_.issues.push_back(std::move(missing));
    }
    ResolvedDependency resolved{.owner = id,
                                .role = role,
                                .targets = std::move(selection.targets),
                                .alternatives = std::move(selection.alternatives)};
    for (const auto& target : resolved.targets) {
      const auto* provider = assets_.asset(target.asset);
      if (provider == nullptr || !matchesRole(*provider, role)) {
        issue(id, "invalid-dependency", "Asset dependency refers to a missing or wrong-type provider", Severity::Error);
        continue;
      }
      if (mode_ == ResolutionMode::Manual &&
          std::ranges::find(membersFor(selected_, role), target.asset) == membersFor(selected_, role).end()) {
        issue(id, "invalid-dependency", "Asset dependency selected a provider outside the manual collection",
              Severity::Error);
        continue;
      }
      auto& members = membersFor(result_.members, role);
      if (std::ranges::find(members, target.asset) == members.end()) {
        members.push_back(target.asset);
      }
      visit(target.asset);
    }
    result_.dependencies.push_back(std::move(resolved));
  }

  void issue(AssetId id, std::string code, std::string message, Severity severity) {
    result_.issues.push_back({.impact = CollectionIssueImpact::Incomplete,
                              .severity = severity,
                              .code = std::move(code),
                              .message = std::move(message),
                              .asset = id});
  }
  const AssetCatalog& assets_;
  DesiredCollection& result_;
  ResolutionMode mode_;
  CollectionMembers selected_;
  std::unordered_set<u32> visited_;
  std::unordered_set<u32> active_;
};

}  // namespace

void resolveDependencies(const AssetCatalog& assets, DesiredCollection& collection, ResolutionMode mode) {
  Resolver resolver(assets, collection, mode);
  if (collection.members.sequence) {
    resolver.visit(*collection.members.sequence);
  }
  // Copy the seeds: resolving a bank may grow membership.
  const auto banks = collection.members.soundBanks;
  for (auto id : banks) {
    resolver.visit(id);
  }
}

std::vector<std::pair<std::string, DesiredCollection>> dependencyCollections(const AssetCatalog& assets,
                                                                             std::span<const AssetId> explicitRoots) {
  std::vector<std::pair<std::string, DesiredCollection>> result;
  for (const auto* sequence : assets.assets<SequenceProgramAsset>()) {
    const auto& recipe = sequence->recipe;
    if (recipe.collectionNamespace.empty() ||
        std::ranges::find(explicitRoots, sequence->metadata.id) != explicitRoots.end()) {
      continue;
    }
    DesiredCollection collection{.localKey = "asset:" + std::to_string(sequence->metadata.id.value),
                                 .name = sequence->metadata.name,
                                 .members = {.sequence = sequence->metadata.id}};
    resolveDependencies(assets, collection);
    result.emplace_back(recipe.collectionNamespace, std::move(collection));
  }
  return result;
}

}  // namespace vgmtrans::core

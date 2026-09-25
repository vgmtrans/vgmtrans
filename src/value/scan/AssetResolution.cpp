/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/AssetResolution.h"

namespace vgmtrans::core {

namespace {

class Resolver {
public:
  Resolver(const AssetCatalog& assets, DesiredCollection& result, ResolutionMode mode)
      : assets_(assets), result_(result), mode_(mode), selected_(result.members) {}

  void resolve() {
    if (result_.members.sequence) {
      if (const auto* sequence = assets_.asset<SequenceProgramAsset>(*result_.members.sequence)) {
        if (mode_ == ResolutionMode::Manual) {
          // A manual selection replaces all sequence requests, including exact
          // scanner-known references, with one ordered set of banks.
          if (!sequence->recipe.banks.empty() || !selected_.soundBanks.empty()) {
            accept(sequence->metadata, DependencyRole::SoundBank, selectAll(selected_.soundBanks));
          }
        } else {
          requests(sequence->metadata, DependencyRole::SoundBank, sequence->recipe.banks);
        }
        assignBanks(*sequence);
      }
    }
    // The domain has two dependency levels. A bank can only request samples;
    // wrong-type providers are rejected without following another graph edge.
    for (auto id : result_.members.soundBanks) {
      if (const auto* bank = assets_.asset<SoundBankAsset>(id)) {
        requests(bank->metadata, DependencyRole::SamplePool, bank->recipe.samples);
        concreteSamples(*bank);
      }
    }
  }

private:
  void assignBanks(const SequenceProgramAsset& sequence) {
    if (!sequence.recipe.assignBanks) {
      return;
    }
    // One use per selected bank, including explicit collection seeds. Keep
    // placements on the recorded requests; membership remains unchanged.
    std::vector<DependencyTarget> uses;
    for (const auto id : result_.members.soundBanks) {
      DependencyTarget use{id, {}};
      for (const auto& dependency : result_.dependencies) {
        if (dependency.owner == sequence.metadata.id && dependency.role == DependencyRole::SoundBank) {
          const auto found = std::ranges::find(dependency.targets, id, &DependencyTarget::asset);
          if (found != dependency.targets.end()) {
            use.placement = found->placement;
            break;
          }
        }
      }
      uses.push_back(std::move(use));
    }
    try {
      BankAssignmentContext context{assets_, sequence, uses, result_.issues};
      sequence.recipe.assignBanks(context);
    } catch (const std::exception& error) {
      accept(sequence.metadata, DependencyRole::SoundBank,
             DependencySelection::failed(std::string("Bank assignment failed: ") + error.what()));
      return;
    } catch (...) {
      accept(sequence.metadata, DependencyRole::SoundBank, DependencySelection::failed("Bank assignment failed"));
      return;
    }
    // Requests retain their status and alternatives. Attach assignments to the
    // selected targets and add an edge for an explicit seed without a request.
    for (const auto& use : uses) {
      bool found = false;
      for (auto& dependency : result_.dependencies) {
        if (dependency.owner == sequence.metadata.id && dependency.role == DependencyRole::SoundBank) {
          for (auto& target : dependency.targets) {
            if (target.asset == use.asset) {
              target.placement = use.placement;
              found = true;
            }
          }
        }
      }
      if (!found) {
        result_.dependencies.push_back(
            {.owner = sequence.metadata.id, .role = DependencyRole::SoundBank, .targets = {use}});
      }
    }
  }

  std::vector<AssetId>& members(DependencyRole role) {
    return role == DependencyRole::SoundBank ? result_.members.soundBanks : result_.members.samplePools;
  }

  const std::vector<AssetId>& selected(DependencyRole role) const {
    return role == DependencyRole::SoundBank ? selected_.soundBanks : selected_.samplePools;
  }

  void requests(const AssetMetadata& owner, DependencyRole role, const std::vector<DependencyRequest>& requests) {
    for (const auto& request : requests) {
      DependencySelection selection;
      try {
        if (const auto* direct = std::get_if<DependencyTarget>(&request)) {
          selection.add(direct->asset, direct->placement);
        } else {
          selection = std::get<DependencySelector>(request)(
              DependencyContext{assets_, *assets_.asset(owner.id), mode_, selected(role)});
        }
      } catch (const std::exception& error) {
        selection = DependencySelection::failed(std::string("Asset dependency resolution failed: ") + error.what());
      } catch (...) {
        selection = DependencySelection::failed("Asset dependency resolution failed");
      }
      accept(owner, role, selection);
    }
  }

  void concreteSamples(const SoundBankAsset& bank) {
    DependencySelection samples;
    for (const auto& instrument : bank.instruments) {
      for (const auto& region : instrument.regions) {
        const auto owner = region.sample.owner();
        if (!region.sample.valid() || owner == bank.metadata.id) {
          continue;
        }
        const auto linked = [&](const DependencyTarget& target) { return target.asset == owner; };
        const bool declared = std::ranges::any_of(result_.dependencies, [&](const ResolvedDependency& dependency) {
          return dependency.owner == bank.metadata.id && dependency.role == DependencyRole::SamplePool &&
                 std::ranges::any_of(dependency.targets, linked);
        });
        if (!declared && !std::ranges::any_of(samples.targets(), linked)) {
          samples.add(owner);
        }
      }
    }
    if (!samples.targets().empty()) {
      accept(bank.metadata, DependencyRole::SamplePool, samples);
    }
  }

  void accept(const AssetMetadata& owner, DependencyRole role, const DependencySelection& selection) {
    for (auto diagnostic : selection.issues()) {
      if (!diagnostic.asset) {
        diagnostic.asset = owner.id;
      }
      if (!diagnostic.range.valid()) {
        diagnostic.range = owner.range;
      }
      result_.issues.push_back(std::move(diagnostic));
    }
    if (selection.status() == ResolutionStatus::Incomplete && selection.issues().empty()) {
      auto missing = role == DependencyRole::SoundBank ? missingSoundBankIssue() : missingSamplePoolIssue();
      missing.asset = owner.id;
      missing.range = owner.range;
      result_.issues.push_back(std::move(missing));
    }
    ResolvedDependency resolved{.owner = owner.id,
                                .role = role,
                                .status = selection.status(),
                                .targets = selection.targets(),
                                .alternatives = selection.alternatives()};
    for (const auto& target : resolved.targets) {
      const bool correctType = role == DependencyRole::SoundBank
                                   ? assets_.asset<SoundBankAsset>(target.asset) != nullptr
                                   : assets_.asset<SamplePoolAsset>(target.asset) != nullptr;
      const bool allowed =
          mode_ != ResolutionMode::Manual || std::ranges::find(selected(role), target.asset) != selected(role).end();
      if (!correctType || !allowed) {
        resolved.status = ResolutionStatus::Failed;
        result_.issues.push_back({.impact = CollectionIssueImpact::Incomplete,
                                  .severity = Severity::Error,
                                  .code = "invalid-dependency",
                                  .message = !correctType
                                                 ? "Asset dependency refers to a missing or wrong-type provider"
                                                 : "Asset dependency selected a provider outside the manual collection",
                                  .asset = owner.id,
                                  .range = owner.range});
        continue;
      }
      auto& providers = members(role);
      if (std::ranges::find(providers, target.asset) == providers.end()) {
        providers.push_back(target.asset);
      }
    }
    result_.dependencies.push_back(std::move(resolved));
  }

  const AssetCatalog& assets_;
  DesiredCollection& result_;
  ResolutionMode mode_;
  CollectionMembers selected_;
};

}  // namespace

void resolveDependencies(const AssetCatalog& assets, DesiredCollection& collection, ResolutionMode mode) {
  Resolver(assets, collection, mode).resolve();
}

std::vector<std::pair<std::string, DesiredCollection>> dependencyCollections(const AssetCatalog& assets,
                                                                             std::span<const AssetId> explicitRoots) {
  std::vector<std::pair<std::string, DesiredCollection>> result;
  for (const auto* sequence : assets.assets<SequenceProgramAsset>()) {
    if (!sequence->collection || std::ranges::find(explicitRoots, sequence->metadata.id) != explicitRoots.end()) {
      continue;
    }
    const auto& descriptor = *sequence->collection;
    DesiredCollection collection{.localKey = descriptor.key.value.empty()
                                                 ? "asset:" + std::to_string(sequence->metadata.id.value)
                                                 : descriptor.key.value,
                                 .name = descriptor.name.empty() ? sequence->metadata.name : descriptor.name,
                                 .members = {.sequence = sequence->metadata.id, .miscAssets = descriptor.miscAssets}};
    resolveDependencies(assets, collection);
    result.emplace_back(descriptor.key.resolver.empty() ? sequence->metadata.format : descriptor.key.resolver,
                        std::move(collection));
  }
  return result;
}

}  // namespace vgmtrans::core

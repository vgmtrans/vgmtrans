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
        ResolvedDependency banks{.owner = sequence->metadata.id, .role = DependencyRole::SoundBank};
        if (mode_ == ResolutionMode::Manual) {
          // A manual selection replaces all sequence requests, including exact
          // scanner-known references, with one ordered set of banks.
          if (!sequence->recipe.banks.empty() || !selected_.soundBanks.empty()) {
            accept(sequence->metadata, banks, selectAll(selected_.soundBanks));
          }
        } else {
          requests(sequence->metadata, banks, sequence->recipe.banks);
        }
        assignBanks(*sequence, banks);
        if (!sequence->recipe.banks.empty() || sequence->recipe.assignBanks || !banks.targets.empty()) {
          result_.dependencies.push_back(std::move(banks));
        }
      }
    }
    // The domain has two dependency levels. A bank can only request samples;
    // wrong-type providers are rejected without following another graph edge.
    for (auto id : result_.members.soundBanks) {
      if (const auto* bank = assets_.asset<SoundBankAsset>(id)) {
        ResolvedDependency samples{.owner = id, .role = DependencyRole::SamplePool};
        requests(bank->metadata, samples, bank->recipe.samples);
        concreteSamples(*bank, samples);
        if (!bank->recipe.samples.empty() || !samples.targets.empty()) {
          result_.dependencies.push_back(std::move(samples));
        }
      }
    }
  }

private:
  void assignBanks(const SequenceProgramAsset& sequence, ResolvedDependency& banks) {
    if (!sequence.recipe.assignBanks) {
      return;
    }
    try {
      BankAssignmentContext context{assets_, sequence, banks.targets, result_.issues};
      sequence.recipe.assignBanks(context);
    } catch (const std::exception& error) {
      accept(sequence.metadata, banks,
             DependencySelection::failed(std::string("Bank assignment failed: ") + error.what()));
    } catch (...) {
      accept(sequence.metadata, banks, DependencySelection::failed("Bank assignment failed"));
    }
  }

  std::vector<AssetId>& members(DependencyRole role) {
    return role == DependencyRole::SoundBank ? result_.members.soundBanks : result_.members.samplePools;
  }

  const std::vector<AssetId>& selected(DependencyRole role) const {
    return role == DependencyRole::SoundBank ? selected_.soundBanks : selected_.samplePools;
  }

  void requests(const AssetMetadata& owner, ResolvedDependency& resolved,
                const std::vector<DependencyRequest>& requests) {
    for (const auto& request : requests) {
      DependencySelection selection;
      try {
        if (const auto* direct = std::get_if<DependencyTarget>(&request)) {
          selection.add(direct->asset, direct->placement);
        } else {
          selection = std::get<DependencySelector>(request)(
              DependencyContext{assets_, *assets_.asset(owner.id), mode_, selected(resolved.role)});
        }
      } catch (const std::exception& error) {
        selection = DependencySelection::failed(std::string("Asset dependency resolution failed: ") + error.what());
      } catch (...) {
        selection = DependencySelection::failed("Asset dependency resolution failed");
      }
      accept(owner, resolved, selection);
    }
  }

  void concreteSamples(const SoundBankAsset& bank, ResolvedDependency& resolved) {
    DependencySelection samples;
    for (const auto& instrument : bank.instruments) {
      for (const auto& region : instrument.regions) {
        const auto owner = region.sample.owner();
        if (!region.sample.valid() || owner == bank.metadata.id) {
          continue;
        }
        const auto linked = [&](const DependencyTarget& target) { return target.asset == owner; };
        if (!std::ranges::any_of(resolved.targets, linked) && !std::ranges::any_of(samples.targets(), linked)) {
          samples.add(owner);
        }
      }
    }
    if (!samples.targets().empty()) {
      accept(bank.metadata, resolved, samples);
    }
  }

  void accept(const AssetMetadata& owner, ResolvedDependency& resolved, const DependencySelection& selection) {
    const auto role = resolved.role;
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
    resolved.status = std::max(resolved.status, selection.status());
    resolved.alternatives.insert(resolved.alternatives.end(), selection.alternatives().begin(),
                                 selection.alternatives().end());
    for (const auto& target : selection.targets()) {
      // A sequence uses each bank once. Sample inputs may use the same pool at
      // several placements, so those relationships retain every target.
      if (role == DependencyRole::SamplePool ||
          std::ranges::find(resolved.targets, target.asset, &DependencyTarget::asset) == resolved.targets.end()) {
        resolved.targets.push_back(target);
      }
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

std::vector<DesiredCollection> dependencyCollections(const AssetCatalog& assets) {
  std::vector<DesiredCollection> result;
  auto sequences = assets.assets<SequenceProgramAsset>();
  std::ranges::stable_sort(sequences, {}, [](const auto* sequence) { return sequence->metadata.format; });
  for (const auto* sequence : sequences) {
    if (!sequence->collection.enabled) {
      continue;
    }
    const auto& descriptor = sequence->collection;
    DesiredCollection collection{
        .name = descriptor.name.empty() ? sequence->metadata.name : descriptor.name,
        .members = {.sequence = sequence->metadata.id, .miscAssets = descriptor.miscAssets}};
    resolveDependencies(assets, collection);
    result.push_back(std::move(collection));
  }
  return result;
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/AssetResolution.h"

#include <type_traits>

namespace vgmtrans::core {

namespace {

class Resolver {
public:
  Resolver(const AssetCatalog& assets, DesiredCollection& result, ResolutionMode mode)
      : assets_(assets), result_(result), mode_(mode) {}

  void resolve() {
    ResolvedInputs banks;
    if (result_.selection.sequence) {
      if (const auto* sequence = assets_.asset<SequenceProgramAsset>(*result_.selection.sequence)) {
        if (mode_ == ResolutionMode::Manual) {
          // Manual choices replace all requests, including exact scanner links.
          if (!sequence->recipe.banks.empty() || !selected<SoundBankAsset>().empty()) {
            accept<SoundBankAsset>(sequence->metadata, banks, selectAll(selected<SoundBankAsset>()));
          }
        } else {
          requests<SoundBankAsset>(sequence->metadata, banks, sequence->recipe.banks);
        }
      } else {
        banks.status = ResolutionStatus::Failed;
        result_.issues.push_back(missingSequenceIssue(result_.selection.sequence));
      }
    } else if (!selected<SoundBankAsset>().empty()) {
      // Standalone bank preparation has no sequence requests.
      accept<SoundBankAsset>({}, banks, selectAll(selected<SoundBankAsset>()));
    }

    result_.inputs = {.bankStatus = banks.status, .bankAlternatives = std::move(banks.alternatives)};
    for (const auto& target : banks.targets) {
      const auto& bank = *assets_.asset<SoundBankAsset>(target.asset);
      CollectionBank input{.bank = target.asset};
      requests<SamplePoolAsset>(bank.metadata, input.samples, bank.recipe.samples);
      concreteSamples(bank, input.samples);
      result_.inputs.banks.push_back(std::move(input));
    }
  }

private:
  template <class Provider>
  const std::vector<AssetId>& selected() const {
    if constexpr (std::is_same_v<Provider, SoundBankAsset>) {
      return result_.selection.soundBanks;
    } else {
      return result_.selection.samplePools;
    }
  }

  template <class Provider>
  void requests(const AssetMetadata& owner, ResolvedInputs& resolved,
                const std::vector<DependencyRequest>& requests) {
    for (const auto& request : requests) {
      DependencySelection selection;
      try {
        if (const auto* direct = std::get_if<DependencyTarget>(&request)) {
          selection.add(direct->asset, direct->placement);
        } else {
          selection = std::get<DependencySelector>(request)(
              DependencyContext{assets_, *assets_.asset(owner.id), mode_, selected<Provider>()});
        }
      } catch (const std::exception& error) {
        selection = DependencySelection::failed(std::string("Asset dependency resolution failed: ") + error.what());
      } catch (...) {
        selection = DependencySelection::failed("Asset dependency resolution failed");
      }
      accept<Provider>(owner, resolved, selection);
    }
  }

  // Scanners may already identify the pool supplying a region. Include those
  // uses without requiring the format to repeat the same links in its recipe.
  void concreteSamples(const SoundBankAsset& bank, ResolvedInputs& resolved) {
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
      accept<SamplePoolAsset>(bank.metadata, resolved, samples);
    }
  }

  template <class Provider>
  void accept(const AssetMetadata& owner, ResolvedInputs& resolved, const DependencySelection& selection) {
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
      auto missing = std::is_same_v<Provider, SoundBankAsset> ? missingSoundBankIssue() : missingSamplePoolIssue();
      missing.asset = owner.id;
      missing.range = owner.range;
      result_.issues.push_back(std::move(missing));
    }
    // A later successful request cannot hide an earlier unresolved request.
    resolved.status = std::max(resolved.status, selection.status());
    resolved.alternatives.insert(resolved.alternatives.end(), selection.alternatives().begin(),
                                 selection.alternatives().end());
    for (const auto& target : selection.targets()) {
      const bool correctType = assets_.asset<Provider>(target.asset) != nullptr;
      const auto& candidates = selected<Provider>();
      const bool allowed = mode_ != ResolutionMode::Manual ||
                           std::ranges::find(candidates, target.asset) != candidates.end();
      if (!correctType || !allowed) {
        resolved.status = ResolutionStatus::Failed;
        result_.issues.push_back({.severity = Severity::Error,
                                  .code = "invalid-dependency",
                                  .message = !correctType
                                                 ? "Asset dependency refers to a missing or wrong-type provider"
                                                 : "Asset dependency selected a provider outside the manual collection",
                                  .asset = owner.id,
                                  .range = owner.range});
        continue;
      }
      // Banks are used once. Pools can be used at several distinct placements.
      if (std::is_same_v<Provider, SamplePoolAsset> ||
          std::ranges::find(resolved.targets, target.asset, &DependencyTarget::asset) == resolved.targets.end()) {
        resolved.targets.push_back(target);
      }
    }
  }

  const AssetCatalog& assets_;
  DesiredCollection& result_;
  ResolutionMode mode_;
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
        .selection = {.sequence = sequence->metadata.id, .miscAssets = descriptor.miscAssets}};
    resolveDependencies(assets, collection);
    result.push_back(std::move(collection));
  }
  return result;
}

}  // namespace vgmtrans::core

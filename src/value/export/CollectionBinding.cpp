/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionBinding.h"

#include "value/export/ExportDiagnostics.h"
#include "value/export/AssetPreparation.h"
#include "value/export/InstrumentVariants.h"
#include "value/scan/AssetResolution.h"
#include "value/sequence/SequenceVm.h"
#include "value/validation/SynthValidation.h"

#include <exception>
#include <iterator>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::span<const DependencyTarget> inputsFor(const Collection& collection, AssetId owner,
                                                          DependencyRole role) {
  const auto found = std::ranges::find_if(collection.dependencies, [&](const auto& dependency) {
    return dependency.owner == owner && dependency.role == role;
  });
  return found == collection.dependencies.end() ? std::span<const DependencyTarget>{} : found->targets;
}

[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence, const SequenceRuntime& runtime,
                                                const SequenceRenderOptions& options) {
  if (!runtime.valid()) {
    return RenderedCollection{
        .diagnostics =
            {
                exportError("Sequence program has no runtime executor", sequence.metadata.range),
            },
    };
  }

  PerformanceSequence performance;
  try {
    performance = SequenceVm(SequenceVmOptions{
                                 .loopPolicy = options.loopPolicy,
                                 .sequenceLoops = options.sequenceLoops,
                             })
                      .render(sequence.program, runtime);
  } catch (const std::exception& error) {
    return RenderedCollection{
        .diagnostics = {exportError("Sequence rendering failed: " + std::string(error.what()),
                                    sequence.metadata.range)},
    };
  } catch (...) {
    return RenderedCollection{
        .diagnostics = {exportError("Sequence rendering failed", sequence.metadata.range)},
    };
  }
  auto modulation = analyzeSequenceModulation(performance);
  return RenderedCollection{.performance = std::move(performance), .modulation = std::move(modulation)};
}

}  // namespace

BoundCollection::BoundCollection(SessionSnapshot snapshot, CollectionId id, std::string baseName,
                                 const SequenceProgramAsset* sequence, SequenceRuntime sequenceRuntime,
                                 std::vector<SoundBankAsset> soundBanks,
                                 std::vector<const SamplePoolAsset*> samplePools)
    : snapshot_(std::move(snapshot)), id_(id), baseName_(std::move(baseName)), sequence_(sequence),
      sequenceRuntime_(std::move(sequenceRuntime)), soundBanks_(std::move(soundBanks)),
      samplePools_(std::move(samplePools)) {
}

CollectionBindingResult prepareCollection(const SessionSnapshot& snapshot, const Collection& selected) {
  const auto* collection = &selected;
  std::vector<Diagnostic> diagnostics;
  for (const auto& issue : collection->issues) {
    diagnostics.push_back(Diagnostic{
        .severity = issue.severity,
        .code = issue.code,
        .message = issue.message,
        .range = issue.range,
        .object = issue.asset && snapshot.asset(*issue.asset) != nullptr
                      ? std::optional<ObjectRef>{ObjectRefs::asset(*issue.asset)}
                      : std::nullopt,
    });
  }

  const CollectionMembers& members = collection->members;
  std::string baseName =
      collection->name.empty() ? "collection-" + std::to_string(collection->id.value) : collection->name;
  const SequenceProgramAsset* sequence = nullptr;
  SequenceRuntime sequenceRuntime;
  bool failed = std::ranges::any_of(collection->dependencies, [](const ResolvedDependency& dependency) {
    return dependency.status == ResolutionStatus::Failed;
  });
  if (members.sequence) {
    sequence = snapshot.asset<SequenceProgramAsset>(*members.sequence);
    if (sequence == nullptr) {
      diagnostics.push_back(exportError("Collection sequence asset was not found"));
      failed = true;
    } else {
      sequenceRuntime = sequence->program.runtime;
    }
  }

  std::vector<SoundBankAsset> soundBanks;
  soundBanks.reserve(members.soundBanks.size());
  for (const AssetId assetId : members.soundBanks) {
    if (const auto* bank = snapshot.asset<SoundBankAsset>(assetId)) {
      soundBanks.push_back(*bank);
    } else {
      diagnostics.push_back(exportError("Collection sound bank asset was not found"));
      failed = true;
    }
  }
  std::vector<const SamplePoolAsset*> samplePools;
  samplePools.reserve(members.samplePools.size());
  for (const AssetId assetId : members.samplePools) {
    if (const auto* samples = snapshot.asset<SamplePoolAsset>(assetId)) {
      samplePools.push_back(samples);
    } else {
      diagnostics.push_back(exportError("Collection sample pool asset was not found"));
      failed = true;
    }
  }
  for (const AssetId assetId : members.miscAssets) {
    if (snapshot.asset<MiscAsset>(assetId) == nullptr) {
      diagnostics.push_back(exportError("Collection miscellaneous asset was not found"));
      failed = true;
    }
  }

  for (const auto& dependency : collection->dependencies) {
    const bool ownerSelected =
        dependency.role == DependencyRole::SoundBank
            ? members.sequence == dependency.owner
            : std::ranges::find(members.soundBanks, dependency.owner) != members.soundBanks.end();
    if (!ownerSelected) {
      diagnostics.push_back(exportError("Dependency owner is not a selected sequence or sound bank"));
      failed = true;
    }
    const auto& providers = dependency.role == DependencyRole::SoundBank ? members.soundBanks : members.samplePools;
    for (const auto& target : dependency.targets) {
      if (std::ranges::find(providers, target.asset) == providers.end()) {
        diagnostics.push_back(exportError("Dependency provider is not a selected collection member"));
        failed = true;
      }
    }
  }
  if (!failed) {
    try {
      const auto bankUses = sequence == nullptr
                                ? std::span<const DependencyTarget>{}
                                : inputsFor(*collection, sequence->metadata.id, DependencyRole::SoundBank);
      for (size_t i = 0; i < soundBanks.size(); ++i) {
        auto& bank = soundBanks[i];
        const auto& prepare = snapshot.asset<SoundBankAsset>(members.soundBanks[i])->prepare;
        if (!prepare) {
          continue;
        }
        const auto inputs = inputsFor(*collection, bank.metadata.id, DependencyRole::SamplePool);
        const u32 index =
            static_cast<u32>(std::count_if(soundBanks.begin(), soundBanks.begin() + i, [&](const auto& previous) {
              return previous.metadata.format == bank.metadata.format;
            }));
        const auto use = std::ranges::find(bankUses, bank.metadata.id, &DependencyTarget::asset);
        BankPreparationContext context{
            bank, index, inputs, samplePools, diagnostics, use == bankUses.end() ? AssetPrivateData{} : use->placement};
        prepare(context);
        if (context.failed) {
          failed = true;
          break;
        }
      }
      if (!failed && sequence != nullptr && sequence->prepare) {
        SequencePreparationContext context{*sequence, sequenceRuntime, soundBanks, diagnostics, bankUses};
        sequence->prepare(context);
        failed = context.failed;
      }
    } catch (const std::exception& error) {
      diagnostics.push_back(exportError(std::string("Asset preparation failed: ") + error.what()));
      failed = true;
    } catch (...) {
      diagnostics.push_back(exportError("Asset preparation failed"));
      failed = true;
    }
  }

  if (!failed) {
    for (size_t index = 0; index < soundBanks.size(); ++index) {
      const auto& metadata = soundBanks[index].metadata;
      const auto* original = snapshot.asset<SoundBankAsset>(members.soundBanks[index]);
      if (original == nullptr || metadata.id != original->metadata.id || metadata.format != original->metadata.format) {
        diagnostics.push_back(exportError("Asset preparation changed sound bank identity, format, or order"));
        failed = true;
        break;
      }
    }
  }
  if (!failed) {
    for (const auto& bank : soundBanks) {
      auto validation = validateSoundBank(bank);
      validation.merge(validateSampleReferences(bank, samplePools));
      auto additions = validation.takeDiagnostics();
      failed = failed || !additions.empty();
      diagnostics.insert(diagnostics.end(), std::make_move_iterator(additions.begin()),
                         std::make_move_iterator(additions.end()));
    }
  }
  if (failed) {
    return CollectionBindingResult{.diagnostics = std::move(diagnostics)};
  }
  return CollectionBindingResult{
      .collection = BoundCollection(snapshot, collection->id, std::move(baseName), sequence, std::move(sequenceRuntime),
                                    std::move(soundBanks), std::move(samplePools)),
      .diagnostics = std::move(diagnostics),
  };
}

CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collectionId) {
  const auto* collection = snapshot.collection(collectionId);
  if (collection == nullptr) {
    return {.diagnostics = {exportError("CollectionId was not found in the SessionSnapshot")}};
  }
  return prepareCollection(snapshot, *collection);
}

CollectionBindingResult bindSoundBank(const SessionSnapshot& snapshot, AssetId id) {
  const auto* bank = snapshot.asset<SoundBankAsset>(id);
  if (bank == nullptr) {
    return {.diagnostics = {exportError("Sound bank asset was not found")}};
  }
  DesiredCollection selected{.name = bank->metadata.name, .members = {.soundBanks = {id}}};
  resolveDependencies(AssetCatalog{snapshot.sources(), snapshot.assets()}, selected);
  return prepareCollection(snapshot, Collection{.name = std::move(selected.name),
                                                .members = std::move(selected.members),
                                                .issues = std::move(selected.issues),
                                                .dependencies = std::move(selected.dependencies)});
}

RenderedCollection renderSequence(const SequenceProgramAsset& sequence, const SequenceRenderOptions& options) {
  return renderSequence(sequence, sequence.program.runtime, options);
}

RenderedCollection renderCollection(const BoundCollection& collection, const SequenceRenderOptions& options) {
  if (collection.sequence_ == nullptr) {
    return RenderedCollection{
        .diagnostics = {exportError("Collection does not reference a sequence asset")},
    };
  }
  return renderSequence(*collection.sequence_, collection.sequenceRuntime_, options);
}

CollectionWorkspace::CollectionWorkspace(BoundCollection collection, std::vector<Diagnostic> diagnostics)
    : collection(std::move(collection)), diagnostics(std::move(diagnostics)) {
}

void CollectionWorkspace::render(const SequenceRenderOptions& options, DynamicEnvelopePolicy dynamicEnvelopes,
                                 bool materializeSignedStereo) {
  rendering = renderCollection(collection, options);
  if (!rendering.performance ||
      (dynamicEnvelopes != DynamicEnvelopePolicy::InstrumentVariants && !materializeSignedStereo)) {
    return;
  }

  auto materialized = materializeInstrumentVariants(
      *rendering.performance, collection.soundBanks_,
      InstrumentVariantOptions{
          .dynamicEnvelopes = dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants,
          .signedStereo = materializeSignedStereo,
      });
  exportPerformance = std::move(materialized.performance);
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(materialized.diagnostics.begin()),
                     std::make_move_iterator(materialized.diagnostics.end()));
}

void CollectionWorkspace::prepareModulation(ModulationConversionPolicy conversion, ModulationScalingPolicy scaling) {
  if (conversion != ModulationConversionPolicy::SynthModulators || !rendering.performance) {
    return;
  }
  if (rendering.modulation.hasSynthModulation()) {
    for (auto& soundBank : collection.soundBanks_) {
      applySequenceModulation(soundBank, rendering.modulation);
    }
  }
  if (scaling == ModulationScalingPolicy::ObservedSequenceRange) {
    modulationUsage = analyzePerformanceModulationUsage(*performance(), &rendering.modulation);
  }
}

const PerformanceSequence* CollectionWorkspace::performance() const noexcept {
  if (exportPerformance) {
    return &*exportPerformance;
  }
  return rendering.performance ? &*rendering.performance : nullptr;
}

std::vector<const SoundBankAsset*> CollectionWorkspace::soundBankView() const {
  std::vector<const SoundBankAsset*> view;
  view.reserve(collection.soundBanks_.size());
  for (const auto& soundBank : collection.soundBanks_) {
    view.push_back(&soundBank);
  }
  return view;
}

}  // namespace vgmtrans::core

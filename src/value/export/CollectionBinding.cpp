/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionBinding.h"

#include "value/export/ExportDiagnostics.h"
#include "value/export/AssetPreparation.h"
#include "value/scan/AssetResolution.h"
#include "value/sequence/SequenceVm.h"
#include "value/validation/SynthValidation.h"

#include <exception>
#include <iterator>
#include <utility>

namespace vgmtrans::core {

namespace {

// Collection membership lists each asset once; dependencies say which inputs
// belong to this particular sequence or bank, including their placement settings.
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

// Prepare the selected assets for playback or export using the choices made
// during resolution. Check their references, prepare private bank copies, configure
// the sequence, then validate the resulting instruments and sample references.
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
  // Missing or ambiguous matches may still leave useful inputs. Only a Failed
  // outcome stops preparation here; diagnostic severity does not decide this.
  bool failed = collection->resolutionStatus() == ResolutionStatus::Failed;
  if (members.sequence) {
    sequence = snapshot.asset<SequenceProgramAsset>(*members.sequence);
    if (sequence == nullptr) {
      diagnostics.push_back(exportError("Collection sequence asset was not found"));
      failed = true;
    } else {
      sequenceRuntime = sequence->program.runtime;
    }
  }

  // Callbacks change private bank copies so preparing one collection cannot
  // change another. Sample pools stay read-only and are kept alive by the snapshot.
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

  // Every recorded use must connect selected members. A valid asset elsewhere
  // in the snapshot is not enough: it must belong to this collection too.
  for (const auto& dependency : collection->dependencies) {
    const bool ownerSelected = dependency.role == DependencyRole::SamplePool
                                   ? std::ranges::find(members.soundBanks, dependency.owner) != members.soundBanks.end()
                                   : members.sequence == dependency.owner;
    if (!ownerSelected) {
      diagnostics.push_back(exportError("Dependency owner is not a selected sequence or sound bank"));
      failed = true;
    }
    const auto& providers = dependency.role == DependencyRole::SamplePool  ? members.samplePools
                            : dependency.role == DependencyRole::SoundBank ? members.soundBanks
                                                                           : members.miscAssets;
    for (const auto& target : dependency.targets) {
      if (std::ranges::find(providers, target.asset) == providers.end()) {
        diagnostics.push_back(exportError("Dependency provider is not a selected collection member"));
        failed = true;
      }
    }
  }
  if (!failed) {
    try {
      // The sequence's bank list carries its settings for each bank. Each bank
      // also has its own sample input list, retrieved separately below.
      const auto bankUses = sequence == nullptr
                                ? std::span<const DependencyTarget>{}
                                : inputsFor(*collection, sequence->metadata.id, DependencyRole::SoundBank);
      for (size_t i = 0; i < soundBanks.size(); ++i) {
        auto& bank = soundBanks[i];
        // Keep the callback on the original asset so it stays alive even if it
        // replaces the bank copy. Each bank uses its own format's preparation.
        const auto& prepare = snapshot.asset<SoundBankAsset>(members.soundBanks[i])->prepare;
        if (!prepare) {
          continue;
        }
        const auto inputs = inputsFor(*collection, bank.metadata.id, DependencyRole::SamplePool);
        // Banks of other formats must not shift this format's bank numbers.
        const u32 index =
            static_cast<u32>(std::count_if(soundBanks.begin(), soundBanks.begin() + i, [&](const auto& previous) {
              return previous.metadata.format == bank.metadata.format;
            }));
        const auto use = std::ranges::find(bankUses, bank.metadata.id, &DependencyTarget::asset);
        BankPreparationContext context{
            bank, index, inputs, samplePools, diagnostics, use == bankUses.end() ? AssetPrivateData{} : use->placement};
        prepare(context);
      }
      // Sequence settings may depend on the banks' prepared instruments and samples.
      if (sequence != nullptr && sequence->prepare) {
        SequencePreparationContext context{*sequence, soundBanks, diagnostics, bankUses};
        if (auto replacement = sequence->prepare(context)) {
          if (!sequenceRuntime.valid()) {
            context.fail("Collection binding cannot replace a sequence runtime with no executor");
          }
          if (!replacement->valid()) {
            context.fail("Collection binding produced a replacement sequence runtime with no executor");
          }
          // Settings may change, but the scanned commands still need the same interpreter.
          if (sequenceRuntime.execute != replacement->execute) {
            context.fail("Collection binding produced an incompatible sequence runtime family");
          }
          sequenceRuntime = std::move(*replacement);
        }
      }
    } catch (detail::PreparationFailure& failure) {
      // context.fail() ends the whole pass; no later callback runs after it.
      diagnostics.push_back(std::move(failure.diagnostic));
      failed = true;
    } catch (const std::exception& error) {
      diagnostics.push_back(exportError(std::string("Asset preparation failed: ") + error.what()));
      failed = true;
    } catch (...) {
      diagnostics.push_back(exportError("Asset preparation failed"));
      failed = true;
    }
  }

  if (!failed) {
    // Preparation may change a bank's contents, but its ID, format, and place
    // in the selected order must still agree with the stored dependencies.
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
    // Check sample references after preparation has connected each instrument
    // to its samples; scanned banks are allowed to leave those links unfinished.
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
    // Discard the private copies instead of exposing a partly prepared collection.
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

PreparedCollection::PreparedCollection(BoundCollection collection, const CollectionPreparationOptions& options)
    : id(collection.id_), baseName(std::move(collection.baseName_)),
      sequenceId(collection.sequence_ ? std::optional{collection.sequence_->metadata.id} : std::nullopt),
      samplePools(std::move(collection.samplePools_)), snapshot_(std::move(collection.snapshot_)) {
  if (options.sequence) {
    rendering = renderCollection(collection, *options.sequence);
  }
  if (!rendering.performance) {
    soundBanks_ = std::make_shared<const std::vector<SoundBankAsset>>(std::move(collection.soundBanks_));
    return;
  }
  if (options.modulationConversion == ModulationConversionPolicy::SynthModulators &&
      rendering.modulation.hasSynthModulation()) {
    for (auto& bank : collection.soundBanks_) applySequenceModulation(bank, rendering.modulation);
  }
  performance_ = preparePerformance(*rendering.performance, std::move(collection.soundBanks_), options.variants);
  soundBanks_ = performance_->soundBanks_;
  if (options.modulationConversion == ModulationConversionPolicy::SynthModulators &&
      options.modulationScaling == ModulationScalingPolicy::ObservedSequenceRange) {
    modulationUsage = analyzePerformanceModulationUsage(performance_->performance(), &rendering.modulation);
  }
}

std::vector<const SoundBankAsset*> PreparedCollection::soundBankView() const {
  std::vector<const SoundBankAsset*> view;
  view.reserve(soundBanks().size());
  for (const auto& soundBank : soundBanks()) {
    view.push_back(&soundBank);
  }
  return view;
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionPreparation.h"

#include "value/export/ExportDiagnostics.h"
#include "value/scan/FormatRegistry.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <string_view>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] const FormatModule* preparationModule(const Collection& collection, const SequenceProgramAsset* sequence,
                                                    const FormatRegistry& formats) {
  if (collection.origin == CollectionOrigin::UserCreated && sequence != nullptr) {
    const auto* module = formats.findModule(sequence->metadata.format);
    if (module != nullptr && module->prepareCollection) {
      return module;
    }
  }

  const auto found = std::ranges::find_if(formats.modules(), [&](const FormatModule& module) {
    const std::string_view resolver =
        module.collectionResolverId.empty() ? std::string_view(module.name) : module.collectionResolverId;
    return module.prepareCollection && resolver == collection.key.resolver;
  });
  return found == formats.modules().end() ? nullptr : &*found;
}

}  // namespace

std::vector<const InstrumentSetAsset*> PreparedCollection::instrumentView() const {
  std::vector<const InstrumentSetAsset*> view;
  view.reserve(instrumentSets.size());
  for (const auto& instruments : instrumentSets) {
    view.push_back(&instruments);
  }
  return view;
}

PreparedCollection prepareCollection(const SessionSnapshot& snapshot, CollectionId collectionId,
                                     const SourceStore& sources, const FormatRegistry& formats) {
  PreparedCollection prepared;
  prepared.collection = snapshot.collection(collectionId);
  if (prepared.collection == nullptr) {
    prepared.diagnostics.collection.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
    return prepared;
  }

  const CollectionMembers& members = prepared.collection->members;
  prepared.baseName = prepared.collection->name.empty() ? "collection-" + std::to_string(prepared.collection->id.value)
                                                        : prepared.collection->name;
  if (members.sequence) {
    prepared.sequence = snapshot.asset<SequenceProgramAsset>(*members.sequence);
    if (prepared.sequence == nullptr) {
      prepared.diagnostics.sequence.push_back(exportError("Collection sequence asset was not found"));
    }
  }

  prepared.instrumentSets.reserve(members.instrumentSets.size());
  for (const AssetId assetId : members.instrumentSets) {
    if (const auto* instruments = snapshot.asset<InstrumentSetAsset>(assetId)) {
      prepared.instrumentSets.push_back(*instruments);
    } else {
      prepared.diagnostics.instrumentSets.push_back(exportError("Collection instrument set asset was not found"));
    }
  }
  prepared.sampleCollections.reserve(members.sampleCollections.size());
  for (const AssetId assetId : members.sampleCollections) {
    if (const auto* samples = snapshot.asset<SampleCollectionAsset>(assetId)) {
      prepared.sampleCollections.push_back(samples);
    } else {
      prepared.diagnostics.sampleCollections.push_back(exportError("Collection sample collection asset was not found"));
    }
  }

  if (const auto* module = preparationModule(*prepared.collection, prepared.sequence, formats)) {
    try {
      auto result = module->prepareCollection(CollectionPrepareContext{
          .sources = sources,
          .snapshot = snapshot,
          .collection = *prepared.collection,
      });
      prepared.diagnostics.collection.insert(prepared.diagnostics.collection.end(),
                                             std::make_move_iterator(result.diagnostics.begin()),
                                             std::make_move_iterator(result.diagnostics.end()));
      prepared.finalizePerformance = std::move(result.finalizePerformance);
      if (result.replacementInstrumentSets) {
        prepared.instrumentSets = std::move(*result.replacementInstrumentSets);
      }
    } catch (const std::exception& error) {
      prepared.diagnostics.collection.push_back(
          exportError(module->name + " collection preparation failed: " + error.what()));
    }
  }
  return prepared;
}

RenderedCollection renderSequence(const SequenceProgramAsset& sequence, const SequenceRenderOptions& options,
                                  const FinalizeCollectionPerformance* finalize) {
  if (!sequence.program.runtime.valid()) {
    return RenderedCollection{
        .diagnostics =
            {
                exportError("Sequence program has no runtime executor", validDiagnosticRange(sequence.metadata.range)),
            },
    };
  }

  auto performance = SequenceVm(SequenceVmOptions{
                                    .loopPolicy = options.loopPolicy,
                                    .sequenceLoops = options.sequenceLoops,
                                })
                         .render(sequence.program);
  if (finalize != nullptr && *finalize) {
    try {
      (*finalize)(performance);
    } catch (const std::exception& error) {
      auto diagnostics = std::move(performance.diagnostics);
      diagnostics.push_back(exportError("Collection performance finalization failed: " + std::string(error.what()),
                                        validDiagnosticRange(sequence.metadata.range)));
      return RenderedCollection{.diagnostics = std::move(diagnostics)};
    } catch (...) {
      auto diagnostics = std::move(performance.diagnostics);
      diagnostics.push_back(
          exportError("Collection performance finalization failed", validDiagnosticRange(sequence.metadata.range)));
      return RenderedCollection{.diagnostics = std::move(diagnostics)};
    }
  }
  auto modulation = analyzeSequenceModulation(performance);
  return RenderedCollection{.performance = std::move(performance), .modulation = std::move(modulation)};
}

RenderedCollection renderCollection(const PreparedCollection& prepared, const SequenceRenderOptions& options) {
  if (prepared.sequence == nullptr) {
    auto diagnostics = prepared.diagnostics.sequence;
    if (diagnostics.empty()) {
      diagnostics.push_back(exportError("Collection does not reference a sequence asset"));
    }
    return RenderedCollection{.diagnostics = std::move(diagnostics)};
  }
  return renderSequence(*prepared.sequence, options, &prepared.finalizePerformance);
}

std::optional<DynamicEnvelopeMaterialization> materializeCollectionDynamicEnvelopes(PreparedCollection& prepared,
                                                                                    const RenderedCollection& rendering,
                                                                                    DynamicEnvelopePolicy policy) {
  if (policy != DynamicEnvelopePolicy::InstrumentVariants || !rendering.performance) {
    return std::nullopt;
  }
  auto materialization = materializeDynamicEnvelopes(*rendering.performance, prepared.instrumentSets);
  prepared.diagnostics.collection.insert(prepared.diagnostics.collection.end(), materialization.diagnostics.begin(),
                                         materialization.diagnostics.end());
  return materialization;
}

void applyCollectionSequenceModulation(PreparedCollection& prepared, const SequenceModulationProfile& profile) {
  if (!profile.hasSynthModulation() || prepared.instrumentSets.empty()) {
    return;
  }
  for (auto& instrumentSet : prepared.instrumentSets) {
    applySequenceModulation(instrumentSet, profile);
  }
}

}  // namespace vgmtrans::core

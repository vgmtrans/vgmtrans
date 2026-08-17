/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionResolution.h"

#include "value/export/ExportDiagnostics.h"
#include "value/scan/FormatRegistry.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] const FormatModule* bindingModule(const Collection& collection, const SequenceProgramAsset* sequence,
                                                const FormatRegistry& formats) {
  if (collection.origin == CollectionOrigin::UserCreated && sequence != nullptr) {
    const auto* module = formats.findModule(sequence->metadata.format);
    if (module != nullptr && module->bindCollection) {
      return module;
    }
  }

  const auto found = std::ranges::find_if(formats.modules(), [&](const FormatModule& module) {
    const std::string_view resolver =
        module.collectionResolverId.empty() ? std::string_view(module.name) : module.collectionResolverId;
    return module.bindCollection && resolver == collection.key.resolver;
  });
  return found == formats.modules().end() ? nullptr : &*found;
}

[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence, const SequenceRuntime& runtime,
                                                const SequenceRenderOptions& options) {
  if (!runtime.valid()) {
    return RenderedCollection{
        .diagnostics =
            {
                exportError("Sequence program has no runtime executor", validDiagnosticRange(sequence.metadata.range)),
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
                                    validDiagnosticRange(sequence.metadata.range))},
    };
  } catch (...) {
    return RenderedCollection{
        .diagnostics = {exportError("Sequence rendering failed", validDiagnosticRange(sequence.metadata.range))},
    };
  }
  auto modulation = analyzeSequenceModulation(performance);
  return RenderedCollection{.performance = std::move(performance), .modulation = std::move(modulation)};
}

}  // namespace

ResolvedCollection::ResolvedCollection(SessionSnapshot snapshot, CollectionId id, std::string baseName,
                                       const SequenceProgramAsset* sequence, SequenceRuntime sequenceRuntime,
                                       std::vector<InstrumentSetAsset> instrumentSets,
                                       std::vector<const SampleCollectionAsset*> sampleCollections,
                                       CollectionResolutionDiagnostics diagnostics)
    : snapshot_(std::move(snapshot)), id_(id), baseName_(std::move(baseName)), sequence_(sequence),
      sequenceRuntime_(std::move(sequenceRuntime)), instrumentSets_(std::move(instrumentSets)),
      sampleCollections_(std::move(sampleCollections)), diagnostics_(std::move(diagnostics)) {
}

ResolvedCollection resolveCollection(const SessionSnapshot& snapshot, CollectionId collectionId,
                                     const SourceStore& sources, const FormatRegistry& formats) {
  CollectionResolutionDiagnostics diagnostics;
  const Collection* collection = snapshot.collection(collectionId);
  if (collection == nullptr) {
    diagnostics.collection.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
    return ResolvedCollection(snapshot, {}, {}, nullptr, {}, {}, {}, std::move(diagnostics));
  }

  const CollectionMembers& members = collection->members;
  std::string baseName =
      collection->name.empty() ? "collection-" + std::to_string(collection->id.value) : collection->name;
  const SequenceProgramAsset* sequence = nullptr;
  SequenceRuntime sequenceRuntime;
  if (members.sequence) {
    sequence = snapshot.asset<SequenceProgramAsset>(*members.sequence);
    if (sequence == nullptr) {
      diagnostics.sequence.push_back(exportError("Collection sequence asset was not found"));
    } else {
      sequenceRuntime = sequence->program.runtime;
    }
  }

  std::vector<InstrumentSetAsset> instrumentSets;
  instrumentSets.reserve(members.instrumentSets.size());
  for (const AssetId assetId : members.instrumentSets) {
    if (const auto* instruments = snapshot.asset<InstrumentSetAsset>(assetId)) {
      instrumentSets.push_back(*instruments);
    } else {
      diagnostics.instrumentSets.push_back(exportError("Collection instrument set asset was not found"));
    }
  }
  std::vector<const SampleCollectionAsset*> sampleCollections;
  sampleCollections.reserve(members.sampleCollections.size());
  for (const AssetId assetId : members.sampleCollections) {
    if (const auto* samples = snapshot.asset<SampleCollectionAsset>(assetId)) {
      sampleCollections.push_back(samples);
    } else {
      diagnostics.sampleCollections.push_back(exportError("Collection sample collection asset was not found"));
    }
  }

  if (const auto* module = bindingModule(*collection, sequence, formats)) {
    try {
      CollectionBindingContext context{
          .sources = sources,
          .sequence = sequence,
          .sequenceRuntime = sequenceRuntime,
          .instrumentSets = instrumentSets,
          .sampleCollections = sampleCollections,
          .diagnostics = diagnostics.collection,
      };
      module->bindCollection(context);
    } catch (const std::exception& error) {
      diagnostics.collection.push_back(exportError(module->name + " collection binding failed: " + error.what()));
    } catch (...) {
      diagnostics.collection.push_back(exportError(module->name + " collection binding failed"));
    }
  }
  return ResolvedCollection(snapshot, collection->id, std::move(baseName), sequence, std::move(sequenceRuntime),
                            std::move(instrumentSets), std::move(sampleCollections), std::move(diagnostics));
}

RenderedCollection renderSequence(const SequenceProgramAsset& sequence, const SequenceRenderOptions& options) {
  return renderSequence(sequence, sequence.program.runtime, options);
}

RenderedCollection renderCollection(const ResolvedCollection& resolved, const SequenceRenderOptions& options) {
  if (resolved.sequence() == nullptr) {
    auto diagnostics = resolved.diagnostics().sequence;
    if (diagnostics.empty()) {
      diagnostics.push_back(exportError("Collection does not reference a sequence asset"));
    }
    return RenderedCollection{.diagnostics = std::move(diagnostics)};
  }
  return renderSequence(*resolved.sequence(), resolved.sequenceRuntime(), options);
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionBinding.h"

#include "value/export/ExportDiagnostics.h"
#include "value/scan/FormatModule.h"
#include "value/sequence/SequenceVm.h"

#include <exception>
#include <utility>

namespace vgmtrans::core {

namespace {

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

BoundCollection::BoundCollection(SessionSnapshot snapshot, CollectionId id, std::string baseName,
                                 const SequenceProgramAsset* sequence, SequenceRuntime sequenceRuntime,
                                 std::vector<InstrumentSetAsset> instrumentSets,
                                 std::vector<const SampleCollectionAsset*> sampleCollections)
    : snapshot_(std::move(snapshot)), id_(id), baseName_(std::move(baseName)), sequence_(sequence),
      sequenceRuntime_(std::move(sequenceRuntime)), instrumentSets_(std::move(instrumentSets)),
      sampleCollections_(std::move(sampleCollections)) {
}

CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collectionId) {
  std::vector<Diagnostic> diagnostics;
  const Collection* collection = snapshot.collection(collectionId);
  if (collection == nullptr) {
    diagnostics.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
    return CollectionBindingResult{.diagnostics = std::move(diagnostics)};
  }

  const CollectionMembers& members = collection->members;
  std::string baseName =
      collection->name.empty() ? "collection-" + std::to_string(collection->id.value) : collection->name;
  const SequenceProgramAsset* sequence = nullptr;
  SequenceRuntime sequenceRuntime;
  bool failed = false;
  if (members.sequence) {
    sequence = snapshot.asset<SequenceProgramAsset>(*members.sequence);
    if (sequence == nullptr) {
      diagnostics.push_back(exportError("Collection sequence asset was not found"));
      failed = true;
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
      diagnostics.push_back(exportError("Collection instrument set asset was not found"));
      failed = true;
    }
  }
  std::vector<const SampleCollectionAsset*> sampleCollections;
  sampleCollections.reserve(members.sampleCollections.size());
  for (const AssetId assetId : members.sampleCollections) {
    if (const auto* samples = snapshot.asset<SampleCollectionAsset>(assetId)) {
      sampleCollections.push_back(samples);
    } else {
      diagnostics.push_back(exportError("Collection sample collection asset was not found"));
      failed = true;
    }
  }

  if (!failed && collection->binder) {
    const std::string bindingName = !collection->key.resolver.empty() ? collection->key.resolver
                                    : sequence != nullptr             ? sequence->metadata.format
                                                                      : "Collection";
    try {
      CollectionBindingContext context{sequence, sequenceRuntime, instrumentSets, sampleCollections, diagnostics};
      collection->binder(context);
      failed = context.failed;
    } catch (const std::exception& error) {
      diagnostics.push_back(exportError(bindingName + " collection binding failed: " + error.what()));
      failed = true;
    } catch (...) {
      diagnostics.push_back(exportError(bindingName + " collection binding failed"));
      failed = true;
    }
  }
  if (failed) {
    return CollectionBindingResult{.diagnostics = std::move(diagnostics)};
  }
  return CollectionBindingResult{
      .collection = BoundCollection(snapshot, collection->id, std::move(baseName), sequence, std::move(sequenceRuntime),
                                    std::move(instrumentSets), std::move(sampleCollections)),
      .diagnostics = std::move(diagnostics),
  };
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

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionBinding.h"

#include "value/export/ExportDiagnostics.h"
#include "value/scan/FormatRegistry.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <exception>
#include <set>
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
    return module.bindCollection && module.collectionResolver() == collection.key.resolver;
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

[[nodiscard]] bool preservesInstrumentMembership(std::span<const InstrumentSetAsset> instrumentSets,
                                                 std::span<const AssetId> expectedIds,
                                                 std::span<const std::string> expectedFormats,
                                                 std::vector<Diagnostic>& diagnostics) {
  for (size_t index = 0; index < instrumentSets.size(); ++index) {
    if (instrumentSets[index].metadata.id == expectedIds[index] &&
        instrumentSets[index].metadata.format == expectedFormats[index]) {
      continue;
    }
    diagnostics.push_back(
        exportError("Collection binder changed the identity or order of an instrument-set member"));
    return false;
  }
  return true;
}

void diagnoseDuplicateInstrumentAddresses(std::span<const InstrumentSetAsset> instrumentSets,
                                          std::vector<Diagnostic>& diagnostics) {
  std::set<std::pair<u32, u32>> addresses;
  std::set<std::pair<u32, u32>> reported;
  for (const auto& instrumentSet : instrumentSets) {
    for (const auto& instrument : instrumentSet.instruments) {
      const auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      const auto key = std::pair{address.bank, address.program};
      if (!addresses.insert(key).second && reported.insert(key).second) {
        diagnostics.push_back(exportWarning("Collection contains duplicate instrument address " +
                                            std::to_string(address.bank) + ":" +
                                            std::to_string(address.program)));
      }
    }
  }
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

CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collectionId,
                                       const FormatRegistry& formats) {
  CollectionBindingDiagnostics diagnostics;
  const Collection* collection = snapshot.collection(collectionId);
  if (collection == nullptr) {
    diagnostics.collection.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
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
      diagnostics.sequence.push_back(exportError("Collection sequence asset was not found"));
      failed = true;
    } else {
      sequenceRuntime = sequence->program.runtime;
    }
  }

  std::vector<InstrumentSetAsset> instrumentSets;
  std::vector<std::string> instrumentFormats;
  instrumentSets.reserve(members.instrumentSets.size());
  instrumentFormats.reserve(members.instrumentSets.size());
  for (const AssetId assetId : members.instrumentSets) {
    if (const auto* instruments = snapshot.asset<InstrumentSetAsset>(assetId)) {
      instrumentSets.push_back(*instruments);
      instrumentFormats.push_back(instruments->metadata.format);
    } else {
      diagnostics.instrumentSets.push_back(exportError("Collection instrument set asset was not found"));
      failed = true;
    }
  }
  std::vector<const SampleCollectionAsset*> sampleCollections;
  sampleCollections.reserve(members.sampleCollections.size());
  for (const AssetId assetId : members.sampleCollections) {
    if (const auto* samples = snapshot.asset<SampleCollectionAsset>(assetId)) {
      sampleCollections.push_back(samples);
    } else {
      diagnostics.sampleCollections.push_back(exportError("Collection sample collection asset was not found"));
      failed = true;
    }
  }

  if (!failed) {
    const auto* module = bindingModule(*collection, sequence, formats);
    if (module != nullptr) {
      try {
        CollectionBindingContext context{
            sequence,
            sequenceRuntime,
            instrumentSets,
            sampleCollections,
            diagnostics.collection,
        };
        module->bindCollection(context);
        failed = context.failed();
        if (!failed) {
          failed = !preservesInstrumentMembership(instrumentSets, members.instrumentSets, instrumentFormats,
                                                  diagnostics.collection);
        }
      } catch (const std::exception& error) {
        diagnostics.collection.push_back(exportError(module->name + " collection binding failed: " + error.what()));
        failed = true;
      } catch (...) {
        diagnostics.collection.push_back(exportError(module->name + " collection binding failed"));
        failed = true;
      }
    }
  }
  if (failed) {
    return CollectionBindingResult{.diagnostics = std::move(diagnostics)};
  }
  diagnoseDuplicateInstrumentAddresses(instrumentSets, diagnostics.collection);
  return CollectionBindingResult{
      .collection = BoundCollection(snapshot, collection->id, std::move(baseName), sequence,
                                    std::move(sequenceRuntime), std::move(instrumentSets),
                                    std::move(sampleCollections)),
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

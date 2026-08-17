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
                                 std::vector<SoundBankAsset> soundBanks,
                                 std::vector<const SamplePoolAsset*> samplePools)
    : snapshot_(std::move(snapshot)), id_(id), baseName_(std::move(baseName)), sequence_(sequence),
      sequenceRuntime_(std::move(sequenceRuntime)), soundBanks_(std::move(soundBanks)),
      samplePools_(std::move(samplePools)) {
}

CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collectionId) {
  std::vector<Diagnostic> diagnostics;
  const Collection* collection = snapshot.collection(collectionId);
  if (collection == nullptr) {
    diagnostics.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
    return CollectionBindingResult{.diagnostics = std::move(diagnostics)};
  }
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

  std::vector<SoundBankAsset> soundBanks;
  std::vector<std::pair<AssetId, std::string>> bankIdentities;
  soundBanks.reserve(members.soundBanks.size());
  bankIdentities.reserve(members.soundBanks.size());
  for (const AssetId assetId : members.soundBanks) {
    if (const auto* bank = snapshot.asset<SoundBankAsset>(assetId)) {
      soundBanks.push_back(*bank);
      bankIdentities.emplace_back(bank->metadata.id, bank->metadata.format);
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

  if (!failed && collection->binder) {
    const std::string bindingName = !collection->key.resolver.empty() ? collection->key.resolver
                                    : sequence != nullptr             ? sequence->metadata.format
                                                                      : "Collection";
    try {
      CollectionBindingContext context{sequence, sequenceRuntime, soundBanks, samplePools, diagnostics};
      collection->binder(context);
      failed = context.failed;
      for (size_t index = 0; index < soundBanks.size(); ++index) {
        const auto& metadata = soundBanks[index].metadata;
        const auto& [id, format] = bankIdentities[index];
        if (metadata.id != id || metadata.format != format) {
          diagnostics.push_back(exportError("Collection binding changed sound bank identity, format, or order"));
          failed = true;
          break;
        }
      }
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
                                    std::move(soundBanks), std::move(samplePools)),
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

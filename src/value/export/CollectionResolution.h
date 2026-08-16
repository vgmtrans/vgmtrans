/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/scan/FormatModule.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

class FormatRegistry;

struct CollectionResolutionDiagnostics {
  std::vector<Diagnostic> collection;
  std::vector<Diagnostic> sequence;
  std::vector<Diagnostic> instrumentSets;
  std::vector<Diagnostic> sampleCollections;
};

// Final, collection-local input to rendering and export. The retained snapshot
// keeps every borrowed asset alive, while private storage prevents exporters
// from confusing target-specific projections with collection binding.
class ResolvedCollection {
public:
  [[nodiscard]] const std::string& baseName() const noexcept { return baseName_; }
  [[nodiscard]] const Collection* collection() const noexcept { return collection_; }
  [[nodiscard]] const SequenceProgramAsset* sequence() const noexcept { return sequence_; }
  [[nodiscard]] const SequenceRuntime& sequenceRuntime() const noexcept { return sequenceRuntime_; }
  [[nodiscard]] const std::vector<InstrumentSetAsset>& instrumentSets() const noexcept { return instrumentSets_; }
  [[nodiscard]] const std::vector<const SampleCollectionAsset*>& sampleCollections() const noexcept {
    return sampleCollections_;
  }
  [[nodiscard]] const CollectionResolutionDiagnostics& diagnostics() const noexcept { return diagnostics_; }

private:
  friend ResolvedCollection resolveCollection(const SessionSnapshot&, CollectionId, const SourceStore&,
                                              const FormatRegistry&);

  ResolvedCollection(SessionSnapshot snapshot, std::string baseName, const Collection* collection,
                     const SequenceProgramAsset* sequence, SequenceRuntime sequenceRuntime,
                     std::vector<InstrumentSetAsset> instrumentSets,
                     std::vector<const SampleCollectionAsset*> sampleCollections,
                     CollectionResolutionDiagnostics diagnostics);

  SessionSnapshot snapshot_;
  std::string baseName_;
  const Collection* collection_ = nullptr;
  const SequenceProgramAsset* sequence_ = nullptr;
  SequenceRuntime sequenceRuntime_;
  std::vector<InstrumentSetAsset> instrumentSets_;
  std::vector<const SampleCollectionAsset*> sampleCollections_;
  CollectionResolutionDiagnostics diagnostics_;
};

struct RenderedCollection {
  std::optional<PerformanceSequence> performance;
  SequenceModulationProfile modulation;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] ResolvedCollection resolveCollection(const SessionSnapshot& snapshot, CollectionId collection,
                                                   const SourceStore& sources, const FormatRegistry& formats);
[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence,
                                                const SequenceRenderOptions& options);
[[nodiscard]] RenderedCollection renderCollection(const ResolvedCollection& resolved,
                                                  const SequenceRenderOptions& options);

}  // namespace vgmtrans::core

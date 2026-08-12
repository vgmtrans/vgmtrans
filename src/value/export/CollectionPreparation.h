/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/DynamicEnvelope.h"
#include "value/export/ExportTypes.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/scan/FormatModule.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

class FormatRegistry;

struct PreparedCollectionDiagnostics {
  std::vector<Diagnostic> collection;
  std::vector<Diagnostic> sequence;
  std::vector<Diagnostic> instrumentSets;
  std::vector<Diagnostic> sampleCollections;
};

struct PreparedCollection {
  std::string baseName;
  const Collection* collection = nullptr;
  const SequenceProgramAsset* sequence = nullptr;
  std::vector<InstrumentSetAsset> instrumentSets;
  std::vector<const SampleCollectionAsset*> sampleCollections;
  PreparedCollectionDiagnostics diagnostics;
  FinalizeCollectionPerformance finalizePerformance;

  [[nodiscard]] std::vector<const InstrumentSetAsset*> instrumentView() const;
};

struct RenderedCollection {
  std::optional<PerformanceSequence> performance;
  SequenceModulationProfile modulation;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] PreparedCollection prepareCollection(const SessionSnapshot& snapshot, CollectionId collection,
                                                   const SourceStore& sources, const FormatRegistry& formats);
[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence, const FormatRegistry& formats,
                                                const SequenceRenderOptions& options,
                                                const FinalizeCollectionPerformance* finalize = nullptr);
[[nodiscard]] RenderedCollection renderCollection(const PreparedCollection& prepared, const FormatRegistry& formats,
                                                  const SequenceRenderOptions& options);
[[nodiscard]] std::optional<DynamicEnvelopeMaterialization> materializeCollectionDynamicEnvelopes(
    PreparedCollection& prepared, const RenderedCollection& rendering, DynamicEnvelopePolicy policy);
void applyCollectionSequenceModulation(PreparedCollection& prepared, const SequenceModulationProfile& profile);

}  // namespace vgmtrans::core

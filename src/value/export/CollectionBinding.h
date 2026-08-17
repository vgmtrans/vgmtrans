/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/model/SessionSnapshot.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct CollectionBindingResult;
struct RenderedCollection;

// Final, collection-local input to rendering and export. The retained snapshot
// keeps every borrowed asset alive, while private storage prevents exporters
// from confusing target-specific projections with collection binding.
class BoundCollection {
public:
  [[nodiscard]] CollectionId id() const noexcept { return id_; }
  [[nodiscard]] const std::string& baseName() const noexcept { return baseName_; }
  [[nodiscard]] bool hasSequence() const noexcept { return sequence_ != nullptr; }
  [[nodiscard]] std::optional<AssetId> sequenceId() const noexcept {
    return sequence_ != nullptr ? std::optional{sequence_->metadata.id} : std::nullopt;
  }
  [[nodiscard]] const std::vector<SoundBankAsset>& soundBanks() const noexcept { return soundBanks_; }
  [[nodiscard]] const std::vector<const SamplePoolAsset*>& samplePools() const noexcept { return samplePools_; }

private:
  friend CollectionBindingResult bindCollection(const SessionSnapshot&, CollectionId);
  friend RenderedCollection renderCollection(const BoundCollection&, const SequenceRenderOptions&);

  BoundCollection(SessionSnapshot snapshot, CollectionId id, std::string baseName, const SequenceProgramAsset* sequence,
                  SequenceRuntime sequenceRuntime, std::vector<SoundBankAsset> soundBanks,
                  std::vector<const SamplePoolAsset*> samplePools);

  SessionSnapshot snapshot_;
  CollectionId id_;
  std::string baseName_;
  const SequenceProgramAsset* sequence_ = nullptr;
  SequenceRuntime sequenceRuntime_;
  std::vector<SoundBankAsset> soundBanks_;
  std::vector<const SamplePoolAsset*> samplePools_;
};

// A BoundCollection exists only after every fatal membership and format-binding
// check succeeds. Diagnostics remain available for both successful warnings and
// failures that intentionally publish no partially bound value.
struct CollectionBindingResult {
  std::optional<BoundCollection> collection;
  std::vector<Diagnostic> diagnostics;
};

struct RenderedCollection {
  std::optional<PerformanceSequence> performance;
  SequenceModulationProfile modulation;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collection);
[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence,
                                                const SequenceRenderOptions& options);
[[nodiscard]] RenderedCollection renderCollection(const BoundCollection& collection,
                                                  const SequenceRenderOptions& options);

}  // namespace vgmtrans::core

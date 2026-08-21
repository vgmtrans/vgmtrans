/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/model/SessionSnapshot.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct CollectionBindingResult;
struct RenderedCollection;
class CollectionWorkspace;

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
  [[nodiscard]] const std::vector<const MiscAsset*>& miscAssets() const noexcept { return miscAssets_; }

private:
  friend CollectionBindingResult bindCollection(const SessionSnapshot&, CollectionId);
  friend RenderedCollection renderCollection(const BoundCollection&, const SequenceRenderOptions&);
  friend class CollectionWorkspace;

  BoundCollection(SessionSnapshot snapshot, CollectionId id, std::string baseName, const SequenceProgramAsset* sequence,
                  SequenceRuntime sequenceRuntime, std::vector<SoundBankAsset> soundBanks,
                  std::vector<const SamplePoolAsset*> samplePools, std::vector<const MiscAsset*> miscAssets);

  SessionSnapshot snapshot_;
  CollectionId id_;
  std::string baseName_;
  const SequenceProgramAsset* sequence_ = nullptr;
  SequenceRuntime sequenceRuntime_;
  std::vector<SoundBankAsset> soundBanks_;
  std::vector<const SamplePoolAsset*> samplePools_;
  std::vector<const MiscAsset*> miscAssets_;
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

// Move-owned preparation shared by playback, ordinary export, and stitching.
// The canonical rendering remains separate from its export-only projection.
// Callers invoke each preparation phase at most once on a fresh instance.
class CollectionWorkspace {
public:
  CollectionWorkspace(BoundCollection collection, std::vector<Diagnostic> diagnostics);

  CollectionWorkspace(const CollectionWorkspace&) = delete;
  CollectionWorkspace(CollectionWorkspace&&) noexcept = default;

  void render(const SequenceRenderOptions& options, DynamicEnvelopePolicy dynamicEnvelopes);
  void prepareSynth(ModulationConversionPolicy conversion, ModulationScalingPolicy scaling);

  [[nodiscard]] const PerformanceSequence* performance() const noexcept;
  [[nodiscard]] std::vector<const SoundBankAsset*> soundBankView() const;
  [[nodiscard]] std::vector<SoundBankAsset>& soundBanks() noexcept { return collection.soundBanks_; }

  BoundCollection collection;
  RenderedCollection rendering;
  std::optional<PerformanceSequence> exportPerformance;
  std::optional<MidiModulationUsage> modulationUsage;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collection);
[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence,
                                                const SequenceRenderOptions& options);
[[nodiscard]] RenderedCollection renderCollection(const BoundCollection& collection,
                                                  const SequenceRenderOptions& options);

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/export/ResolvedPerformance.h"
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

// Prepared inputs for one collection. It owns the changed bank copies and
// sequence settings, and keeps the snapshot alive for the assets it references.
// Playback and export can use it without changing the scanned assets.
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
  friend CollectionBindingResult prepareCollection(const SessionSnapshot&, const Collection&);
  friend RenderedCollection renderCollection(const BoundCollection&, const SequenceRenderOptions&);
  friend class CollectionWorkspace;

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

// Failure returns diagnostics without a collection. Successful preparation can
// still carry warnings, so callers check collection rather than the message list.
struct CollectionBindingResult {
  std::optional<BoundCollection> collection;
  std::vector<Diagnostic> diagnostics;
};

struct RenderedCollection {
  std::optional<PerformanceSequence> performance;
  SequenceModulationProfile modulation;
  std::vector<Diagnostic> diagnostics;
};

// Work shared by playback, export, and stitching. Keep the rendered performance
// separate from changes needed only for export. Call each preparation step at
// most once on a fresh workspace.
class CollectionWorkspace {
public:
  CollectionWorkspace(BoundCollection collection, std::vector<Diagnostic> diagnostics);

  CollectionWorkspace(const CollectionWorkspace&) = delete;
  CollectionWorkspace(CollectionWorkspace&&) noexcept = default;

  void render(const SequenceRenderOptions& options, DynamicEnvelopePolicy dynamicEnvelopes,
              bool materializeSignedStereo = false,
              ModulationConversionPolicy conversion = ModulationConversionPolicy::SynthModulators,
              ModulationScalingPolicy scaling = ModulationScalingPolicy::FullFormatRange);

  [[nodiscard]] const PerformanceSequence* performance() const noexcept;
  [[nodiscard]] std::vector<const SoundBankAsset*> soundBankView() const;
  [[nodiscard]] const std::vector<SoundBankAsset>& soundBanks() const noexcept {
    return exportPerformance ? exportPerformance->soundBanks() : collection.soundBanks_;
  }

  BoundCollection collection;
  RenderedCollection rendering;
  std::optional<ResolvedPerformance> exportPerformance;
  MidiModulationUsage modulationUsage;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collection);
// Resolve a bank's sample inputs and prepare it without requiring a sequence.
[[nodiscard]] CollectionBindingResult bindSoundBank(const SessionSnapshot& snapshot, AssetId bank);
[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence,
                                                const SequenceRenderOptions& options);
[[nodiscard]] RenderedCollection renderCollection(const BoundCollection& collection,
                                                  const SequenceRenderOptions& options);

}  // namespace vgmtrans::core

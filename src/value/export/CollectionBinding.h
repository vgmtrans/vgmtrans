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

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct CollectionBindingResult;
struct RenderedCollection;
class PreparedCollection;

// Prepared inputs for one collection. It owns the changed bank copies and
// sequence settings, and keeps the snapshot alive for the assets it references.
// Playback and export can use it without changing the scanned assets.
class BoundCollection {
public:
  [[nodiscard]] const std::string& baseName() const noexcept { return baseName_; }
  [[nodiscard]] bool hasSequence() const noexcept { return sequence_ != nullptr; }
  [[nodiscard]] const std::vector<SoundBankAsset>& soundBanks() const noexcept { return soundBanks_; }
  [[nodiscard]] const std::vector<const SamplePoolAsset*>& samplePools() const noexcept { return samplePools_; }

private:
  friend CollectionBindingResult prepareCollection(const SessionSnapshot&, const Collection&);
  friend RenderedCollection renderCollection(const BoundCollection&, const SequenceRenderOptions&);
  friend class PreparedCollection;

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

struct CollectionPreparationOptions {
  // Absent for exports that do not need sequence execution, such as WAV.
  std::optional<SequenceRenderOptions> sequence;
  InstrumentPreparationOptions instruments;
  ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators;
  ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange;
};

// Completed export inputs. Construction consumes the private binding and runs
// any requested sequence preparation. Banks and sample owners survive even if
// rendering fails; there is no partially rendered workspace to finish later.
class PreparedCollection {
public:
  explicit PreparedCollection(BoundCollection collection, const CollectionPreparationOptions& options = {});

  [[nodiscard]] const ResolvedPerformance* performance() const noexcept {
    return performance_ ? &*performance_ : nullptr;
  }
  [[nodiscard]] std::vector<const SoundBankAsset*> soundBankView() const;
  [[nodiscard]] const std::vector<SoundBankAsset>& soundBanks() const noexcept { return *soundBanks_; }

  CollectionId id;
  std::string baseName;
  std::optional<AssetId> sequenceId;
  std::vector<const SamplePoolAsset*> samplePools;
  // Retain source events for inspection independently of export adaptations.
  RenderedCollection rendering;
  MidiModulationUsage modulationUsage;

private:
  SessionSnapshot snapshot_;
  // The performance, when present, shares this same immutable allocation.
  std::shared_ptr<const std::vector<SoundBankAsset>> soundBanks_;
  std::optional<ResolvedPerformance> performance_;
};

[[nodiscard]] CollectionBindingResult bindCollection(const SessionSnapshot& snapshot, CollectionId collection);
// Resolve a bank's sample inputs and prepare it without requiring a sequence.
[[nodiscard]] CollectionBindingResult bindSoundBank(const SessionSnapshot& snapshot, AssetId bank);
[[nodiscard]] RenderedCollection renderSequence(const SequenceProgramAsset& sequence,
                                                const SequenceRenderOptions& options);
[[nodiscard]] RenderedCollection renderCollection(const BoundCollection& collection,
                                                  const SequenceRenderOptions& options);

}  // namespace vgmtrans::core

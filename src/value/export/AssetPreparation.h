/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"

#include <algorithm>
#include <span>

namespace vgmtrans::core {

// A sequence configures its private runtime from prepared, read-only banks.
// Logical bank addressing is assigned during resolution and applied by each
// bank's own preparation hook.
struct SequencePreparationContext {
public:
  SequencePreparationContext(const SequenceProgramAsset* sequence, SequenceRuntime& sequenceRuntime,
                             std::span<const SoundBankAsset> soundBanks,
                             std::span<const SamplePoolAsset* const> samplePools,
                             std::span<const MiscAsset* const> miscAssets, std::vector<Diagnostic>& diagnostics,
                             std::span<const DependencyTarget> bankUses = {})
      : sequence(sequence), soundBanks(soundBanks), samplePools(samplePools), miscAssets(miscAssets),
        diagnostics(diagnostics), sequenceRuntime_(sequenceRuntime), bankUses_(bankUses) {}

  const SequenceProgramAsset* sequence;
  std::span<const SoundBankAsset> soundBanks;
  std::span<const SamplePoolAsset* const> samplePools;
  std::span<const MiscAsset* const> miscAssets;
  std::vector<Diagnostic>& diagnostics;
  bool failed = false;

  [[nodiscard]] const SoundBankAsset* soundBank(AssetId id) const noexcept {
    const auto found = std::ranges::find(soundBanks, id, [](const SoundBankAsset& asset) { return asset.metadata.id; });
    return found == soundBanks.end() ? nullptr : &*found;
  }

  template <class Data>
  [[nodiscard]] const Data* bankPlacement(AssetId id) const {
    const auto use = std::ranges::find(bankUses_, id, &DependencyTarget::asset);
    return use == bankUses_.end() ? nullptr : use->placement.template get<Data>();
  }

  [[nodiscard]] const SamplePoolAsset* samplePool(AssetId id) const noexcept {
    const auto found =
        std::ranges::find(samplePools, id, [](const SamplePoolAsset* asset) { return asset->metadata.id; });
    return found == samplePools.end() ? nullptr : *found;
  }

  [[nodiscard]] const MiscAsset* misc(AssetId id) const noexcept {
    const auto found = std::ranges::find(miscAssets, id, [](const MiscAsset* asset) { return asset->metadata.id; });
    return found == miscAssets.end() ? nullptr : *found;
  }

  [[nodiscard]] bool replaceSequenceRuntime(SequenceRuntime replacement) {
    const SourceRange range = sequence != nullptr ? sequence->metadata.range : SourceRange{};
    if (!sequenceRuntime_.valid()) {
      fail("Collection binding cannot replace a sequence runtime with no executor", range);
      return false;
    }
    if (!replacement.valid()) {
      fail("Collection binding produced a replacement sequence runtime with no executor", range);
      return false;
    }
    if (sequenceRuntime_.execute != replacement.execute) {
      fail("Collection binding produced an incompatible sequence runtime family", range);
      return false;
    }
    sequenceRuntime_ = std::move(replacement);
    return true;
  }

  void warning(std::string message, SourceRange range = {}) { report(Severity::Warning, std::move(message), range); }

  void fail(std::string message, SourceRange range = {}) {
    failed = true;
    report(Severity::Error, std::move(message), range);
  }

private:
  void report(Severity severity, std::string message, SourceRange range) {
    diagnostics.push_back(Diagnostic{
        .severity = severity,
        .message = std::move(message),
        .range = range,
    });
  }

  SequenceRuntime& sequenceRuntime_;
  std::span<const DependencyTarget> bankUses_;
};

template <class Data>
struct SampleInput {
  const SamplePoolAsset& asset;
  const Data& data;
  const AssetPrivateData& placement;
};

// The core supplies the bank's selected inputs, not the collection's entire
// sample list. The same pool may appear in several banks' input lists.
struct BankPreparationContext {
  BankPreparationContext(SoundBankAsset& bank, u32 bankIndex, std::span<const DependencyTarget> inputs,
                         std::span<const SamplePoolAsset* const> samplePools, std::vector<Diagnostic>& diagnostics,
                         AssetPrivateData placement = {})
      : bank(bank), bankIndex(bankIndex), inputs(inputs), diagnostics(diagnostics), placement(std::move(placement)),
        samplePools_(samplePools) {}

  SoundBankAsset& bank;
  // Ordinal among banks of this format, in the collection's selected order.
  u32 bankIndex;
  std::span<const DependencyTarget> inputs;
  std::vector<Diagnostic>& diagnostics;
  // Meaning assigned by the selected sequence; empty for standalone banks.
  const AssetPrivateData placement;
  bool failed = false;

  template <class Data>
  [[nodiscard]] std::vector<SampleInput<Data>> samples() {
    std::vector<SampleInput<Data>> result;
    for (const auto& input : inputs) {
      const auto found =
          std::ranges::find(samplePools_, input.asset, [](const SamplePoolAsset* value) { return value->metadata.id; });
      if (found == samplePools_.end()) {
        fail("Bank input refers to a missing sample pool");
        return {};
      }
      const auto* data = (*found)->privateData.template get<Data>();
      if (data == nullptr) {
        fail("Bank sample input is missing its retained format data");
        return {};
      }
      result.push_back({**found, *data, input.placement});
    }
    return result;
  }

  void warning(std::string message, SourceRange range = {}) {
    diagnostics.push_back({.severity = Severity::Warning,
                           .message = std::move(message),
                           .range = range.valid() ? range : bank.metadata.range});
  }
  void fail(std::string message, SourceRange range = {}) {
    failed = true;
    diagnostics.push_back({.severity = Severity::Error,
                           .message = std::move(message),
                           .range = range.valid() ? range : bank.metadata.range});
  }

private:
  std::span<const SamplePoolAsset* const> samplePools_;
};

}  // namespace vgmtrans::core

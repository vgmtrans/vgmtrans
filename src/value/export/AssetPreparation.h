/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace vgmtrans::core {

namespace detail {

// prepareCollection() catches this, reports the diagnostic, and skips the
// remaining callbacks. Formats use context.fail() to stop preparation this way.
struct PreparationFailure {
  Diagnostic diagnostic;
};

}  // namespace detail

template <class Data>
struct BankInput {
  const SoundBankAsset& asset;
  const Data& data;
  const AssetPrivateData placement;
};

// Used by the sequence's prepare callback after all banks have been prepared.
// Banks are read-only here; the callback configures how this sequence plays them.
struct SequencePreparationContext {
public:
  SequencePreparationContext(const SequenceProgramAsset& sequence, std::span<const SoundBankAsset> soundBanks,
                             std::vector<Diagnostic>& diagnostics, std::span<const DependencyTarget> bankUses = {})
      : sequence(sequence), diagnostics(diagnostics), soundBanks_(soundBanks), bankUses_(bankUses) {}

  const SequenceProgramAsset& sequence;
  std::vector<Diagnostic>& diagnostics;

  // Return this format's banks in selected order. Missing format data stops
  // preparation. Each placement describes how the sequence uses that bank.
  template <class Data>
  [[nodiscard]] std::vector<BankInput<Data>> banks(std::string_view format) {
    std::vector<BankInput<Data>> result;
    for (const auto& bank : soundBanks_) {
      if (bank.metadata.format != format) {
        continue;
      }
      const auto* data = bank.privateData.template get<Data>();
      if (data == nullptr) {
        fail("Sequence bank input is missing its retained format data", bank.metadata.range);
      }
      const auto use = std::ranges::find(bankUses_, bank.metadata.id, &DependencyTarget::asset);
      result.push_back({bank, *data, use == bankUses_.end() ? AssetPrivateData{} : use->placement});
    }
    return result;
  }

  void warning(std::string message, SourceRange range = {}) {
    diagnostics.push_back({.severity = Severity::Warning,
                           .message = std::move(message),
                           .range = range.valid() ? range : sequence.metadata.range});
  }

  // Report an error and stop the entire collection's preparation immediately.
  [[noreturn]] void fail(std::string message, SourceRange range = {}) {
    throw detail::PreparationFailure{{
        .severity = Severity::Error,
        .message = std::move(message),
        .range = range.valid() ? range : sequence.metadata.range,
    }};
  }

private:
  std::span<const SoundBankAsset> soundBanks_;
  std::span<const DependencyTarget> bankUses_;
};

template <class Data>
struct SampleInput {
  const SamplePoolAsset& asset;
  const Data& data;
  const AssetPrivateData& placement;
};

// Used by a bank's prepare callback to update its private copy. Its sample
// inputs were chosen during resolution; the shared sample pools remain read-only.
struct BankPreparationContext {
  BankPreparationContext(SoundBankAsset& bank, u32 bankIndex, std::span<const DependencyTarget> inputs,
                         std::span<const SamplePoolAsset* const> samplePools, std::vector<Diagnostic>& diagnostics,
                         AssetPrivateData placement = {})
      : bank(bank), bankIndex(bankIndex), inputs(inputs), diagnostics(diagnostics), placement(std::move(placement)),
        samplePools_(samplePools) {}

  SoundBankAsset& bank;
  // Zero-based index among banks of this format, in the selected order.
  u32 bankIndex;
  std::span<const DependencyTarget> inputs;
  std::vector<Diagnostic>& diagnostics;
  // Settings assigned by the sequence, if any; empty for standalone banks.
  const AssetPrivateData placement;

  // Only this bank's chosen inputs, in order. A pool can appear more than once
  // with different placements, such as different starting sample positions.
  template <class Data>
  [[nodiscard]] std::vector<SampleInput<Data>> samples() {
    std::vector<SampleInput<Data>> result;
    for (const auto& input : inputs) {
      const auto found =
          std::ranges::find(samplePools_, input.asset, [](const SamplePoolAsset* value) { return value->metadata.id; });
      if (found == samplePools_.end()) {
        fail("Bank input refers to a missing sample pool");
      }
      const auto* data = (*found)->privateData.template get<Data>();
      if (data == nullptr) {
        fail("Bank sample input is missing its retained format data");
      }
      result.push_back({**found, *data, input.placement});
    }
    return result;
  }

  // Empty or multiple inputs stop preparation when the format requires one pool.
  template <class Data>
  [[nodiscard]] SampleInput<Data> sample(std::string_view message = "Bank requires exactly one sample input") {
    const auto selected = samples<Data>();
    if (selected.size() != 1) {
      fail(std::string(message));
    }
    return selected.front();
  }

  void warning(std::string message, SourceRange range = {}) {
    diagnostics.push_back({.severity = Severity::Warning,
                           .message = std::move(message),
                           .range = range.valid() ? range : bank.metadata.range});
  }
  // Report an error and stop the entire collection's preparation immediately.
  [[noreturn]] void fail(std::string message, SourceRange range = {}) {
    throw detail::PreparationFailure{{.severity = Severity::Error,
                                      .message = std::move(message),
                                      .range = range.valid() ? range : bank.metadata.range}};
  }

private:
  std::span<const SamplePoolAsset* const> samplePools_;
};

}  // namespace vgmtrans::core

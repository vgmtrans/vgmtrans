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
  const AssetMetadata& metadata;
  std::vector<Instrument>& instruments;
  SamplePool& localSamples;
  const Data& data;
};

// Used by the sequence's prepare callback after all banks have been prepared.
// It may configure the private bank contents and the sequence runtime together.
// Identity stays read-only; only instruments and local samples can change.
struct SequencePreparationContext {
public:
  SequencePreparationContext(const SequenceProgramAsset& sequence, std::span<SoundBankAsset> soundBanks,
                             std::vector<Diagnostic>& diagnostics)
      : sequence(sequence), diagnostics(diagnostics), soundBanks_(soundBanks) {}

  const SequenceProgramAsset& sequence;
  std::vector<Diagnostic>& diagnostics;

  // Return this format's banks in selected order. Missing format data stops
  // preparation. Changes apply only to this collection's prepared copies.
  template <class Data>
  [[nodiscard]] std::vector<BankInput<Data>> banks(std::string_view format) {
    std::vector<BankInput<Data>> result;
    for (auto& bank : soundBanks_) {
      if (bank.metadata.format != format) {
        continue;
      }
      const auto* data = bank.privateData.template get<Data>();
      if (data == nullptr) {
        fail("Sequence bank input is missing its retained format data", bank.metadata.range);
      }
      result.push_back({bank.metadata, bank.instruments, bank.localSamples, *data});
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
  std::span<SoundBankAsset> soundBanks_;
};

template <class Data>
struct SampleInput {
  const SamplePoolAsset& asset;
  const Data& data;
  const AssetPrivateData& placement;
};

// The scanned asset stays read-only. A callback edits only this collection's
// instruments, local samples, and retained format data. External pools are shared.
struct BankPreparationContext {
  BankPreparationContext(const SoundBankAsset& asset, SoundBankAsset& prepared, u32 bankIndex,
                         std::span<const DependencyTarget> inputs,
                         const SessionSnapshot& snapshot, std::vector<Diagnostic>& diagnostics)
      : asset(asset), instruments(prepared.instruments), localSamples(prepared.localSamples),
        privateData(prepared.privateData), bankIndex(bankIndex), inputs(inputs), diagnostics(diagnostics),
        snapshot_(snapshot) {}

  const SoundBankAsset& asset;
  std::vector<Instrument>& instruments;
  SamplePool& localSamples;
  AssetPrivateData& privateData;
  // Zero-based index among banks of this format, in the selected order.
  u32 bankIndex;
  std::span<const DependencyTarget> inputs;
  std::vector<Diagnostic>& diagnostics;

  // Only this bank's chosen inputs, in order. A pool can appear more than once
  // with different placements, such as different starting sample positions.
  template <class Data>
  [[nodiscard]] std::vector<SampleInput<Data>> samples() {
    std::vector<SampleInput<Data>> result;
    for (const auto& input : inputs) {
      const auto* pool = snapshot_.asset<SamplePoolAsset>(input.asset);
      if (pool == nullptr) {
        fail("Bank input refers to a missing sample pool");
      }
      const auto* data = pool->privateData.template get<Data>();
      if (data == nullptr) {
        fail("Bank sample input is missing its retained format data");
      }
      result.push_back({*pool, *data, input.placement});
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
                           .range = range.valid() ? range : asset.metadata.range});
  }
  // Report an error and stop the entire collection's preparation immediately.
  [[noreturn]] void fail(std::string message, SourceRange range = {}) {
    throw detail::PreparationFailure{{.severity = Severity::Error,
                                      .message = std::move(message),
                                      .range = range.valid() ? range : asset.metadata.range}};
  }

private:
  const SessionSnapshot& snapshot_;
};

}  // namespace vgmtrans::core

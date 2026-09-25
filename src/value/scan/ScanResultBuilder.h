/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/model/SourceMap.h"
#include "value/synth/SynthBuilder.h"
#include "value/export/AssetPreparation.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::core {

class ScanResultBuilder;
class ScanSoundBankDraft;
class ScanSamplePoolDraft;
class ScanMiscDraft;

// Drafts are lightweight views into result-owned pending assets. Creating a
// draft is the publication decision: ScanResultBuilder::finish() materializes
// it even when a sound bank or sample pool remains empty.
class ScanSequenceDraft {
public:
  [[nodiscard]] AssetId id() const noexcept { return id_; }
  ScanSequenceDraft& range(SourceRange range);
  ScanSequenceDraft& program(SequenceProgram program);
  // A sequence produces a collection by default, using its own name.
  ScanSequenceDraft& collectionName(std::string name);
  ScanSequenceDraft& withoutCollection();
  ScanSequenceDraft& includeMisc(AssetId asset);
  ScanSequenceDraft& includeMisc(const ScanMiscDraft& asset);
  ScanSequenceDraft& useBank(AssetId bank);
  ScanSequenceDraft& useBank(const ScanSoundBankDraft& bank);
  ScanSequenceDraft& useBanks(DependencySelector select);
  ScanSequenceDraft& assignBanks(BankAssigner assign);
  ScanSequenceDraft& prepare(SequencePreparer prepare);

  template <class Data, class Prepare>
  ScanSequenceDraft& prepare(Prepare prepare);

  template <typename T>
  ScanSequenceDraft& data(T value);

private:
  friend class ScanResultBuilder;

  ScanSequenceDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

class ScanSoundBankDraft {
public:
  [[nodiscard]] AssetId id() const noexcept { return id_; }

  [[nodiscard]] InstrumentSetBuilder& instruments();
  [[nodiscard]] SamplePoolBuilder& localSamples();
  ScanSoundBankDraft& useSamples(AssetId samples);
  ScanSoundBankDraft& useSamples(const ScanSamplePoolDraft& samples);
  ScanSoundBankDraft& useSamples(DependencySelector select);
  ScanSoundBankDraft& prepare(BankPreparer prepare);

  template <class Data, class Prepare>
  ScanSoundBankDraft& prepare(Prepare prepare);

  template <typename T>
  ScanSoundBankDraft& data(T value);

private:
  friend class ScanResultBuilder;

  ScanSoundBankDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

class ScanSamplePoolDraft {
public:
  [[nodiscard]] AssetId id() const noexcept { return id_; }

  [[nodiscard]] SamplePoolBuilder& samples();
  [[nodiscard]] const SamplePoolBuilder& samples() const;

  template <typename T>
  ScanSamplePoolDraft& data(T value);

private:
  friend class ScanResultBuilder;

  ScanSamplePoolDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

class ScanMiscDraft {
public:
  [[nodiscard]] AssetId id() const noexcept { return id_; }
  ScanMiscDraft& payload(std::vector<u8> payload);

  template <typename T>
  ScanMiscDraft& data(T value);

private:
  friend class ScanResultBuilder;

  ScanMiscDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

// Convenience wrapper for the normal scanner path. It still produces ordinary
// ScanResult values, but keeps format modules away from repetitive ID allocation,
// asset metadata setup, and diagnostics.
class ScanResultBuilder {
public:
  ScanResultBuilder(ScanInput input, std::string format);
  ~ScanResultBuilder();

  [[nodiscard]] SourceId source() const noexcept { return input_.source.id; }
  [[nodiscard]] const SourceFile& sourceFile() const noexcept { return input_.source; }
  [[nodiscard]] ByteReader reader() const noexcept { return input_.reader; }
  [[nodiscard]] std::string_view format() const noexcept { return format_; }
  [[nodiscard]] std::string sourceDisplayName() const;
  [[nodiscard]] SourceMapBuilder& sourceMap() noexcept { return sourceMap_; }
  [[nodiscard]] std::vector<Diagnostic>& diagnostics() noexcept { return result_.diagnostics; }

  // Without an explicit range, finish() spans the sequence's owned source
  // annotations and decoded commands in this input. Container bounds can be
  // supplied here or on the draft to keep that range exact.
  [[nodiscard]] ScanSequenceDraft sequence(std::string name, SourceRange range = {});
  [[nodiscard]] ScanSoundBankDraft soundBank(std::string name, SourceRange range = {});
  [[nodiscard]] ScanSamplePoolDraft samplePool(std::string name, SourceRange range = {});
  [[nodiscard]] ScanMiscDraft misc(std::string name, SourceRange range);

  void diagnostic(Diagnostic diagnostic);
  void warning(std::string message, SourceRange range);
  void error(std::string message, SourceRange range);

  [[nodiscard]] ScanResult finish();

private:
  friend class ScanSequenceDraft;
  friend class ScanSoundBankDraft;
  friend class ScanSamplePoolDraft;
  friend class ScanMiscDraft;

  [[nodiscard]] AssetMetadata metadata(AssetId id, std::string name, SourceRange range) const;

  void setPrivateData(size_t slot, AssetPrivateData data);
  void setSequencePreparer(size_t slot, SequencePreparer prepare);
  void setBankPreparer(size_t slot, BankPreparer prepare);

  ScanInput input_;
  std::string format_;
  ScanResult result_;
  SourceMapBuilder sourceMap_;

  struct DraftSlot;
  // Domain-builder entries retain pointers to their builders, so each slot has
  // a stable address even while the list of published drafts grows.
  std::vector<std::unique_ptr<DraftSlot>> drafts_;
};

template <class Data, class Prepare>
ScanSequenceDraft& ScanSequenceDraft::prepare(Prepare callback) {
  return prepare([callback = std::move(callback)](SequencePreparationContext& context) {
    const auto* data = context.sequence == nullptr ? nullptr : context.sequence->privateData.template get<Data>();
    if (data == nullptr) {
      context.fail("Sequence is missing its preparation data");
      return;
    }
    callback(context, *data);
  });
}

template <class Data, class Prepare>
ScanSoundBankDraft& ScanSoundBankDraft::prepare(Prepare callback) {
  return prepare([callback = std::move(callback)](BankPreparationContext& context) {
    const auto* data = context.bank.privateData.template get<Data>();
    if (data == nullptr) {
      context.fail("Bank is missing its preparation data");
      return;
    }
    callback(context, *data);
  });
}

template <typename T>
ScanSequenceDraft& ScanSequenceDraft::data(T value) {
  out_->setPrivateData(slot_, AssetPrivateData::make(std::move(value)));
  return *this;
}

template <typename T>
ScanSoundBankDraft& ScanSoundBankDraft::data(T value) {
  out_->setPrivateData(slot_, AssetPrivateData::make(std::move(value)));
  return *this;
}

template <typename T>
ScanSamplePoolDraft& ScanSamplePoolDraft::data(T value) {
  out_->setPrivateData(slot_, AssetPrivateData::make(std::move(value)));
  return *this;
}

template <typename T>
ScanMiscDraft& ScanMiscDraft::data(T value) {
  out_->setPrivateData(slot_, AssetPrivateData::make(std::move(value)));
  return *this;
}

}  // namespace vgmtrans::core

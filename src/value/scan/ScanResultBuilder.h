/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/model/SourceMap.h"
#include "value/synth/SynthBuilder.h"
#include "value/model/AssetPreparation.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::core {

class ScanResultBuilder;
class ScanSoundBankDraft;
class ScanSamplePoolDraft;

// Drafts are lightweight views into builder-owned pending assets. Creating a
// draft is the publication decision: ScanResultBuilder::finish() materializes
// it even when a sound bank or sample pool remains empty.
class ScanSequenceDraft {
public:
  [[nodiscard]] AssetId id() const noexcept { return pending_->id; }
  ScanSequenceDraft& range(SourceRange range);
  ScanSequenceDraft& program(SequenceProgram program);
  // A sequence produces a collection by default, using its own name.
  ScanSequenceDraft& collectionName(std::string name);
  ScanSequenceDraft& withoutCollection();
  ScanSequenceDraft& includeMisc(AssetId asset);
  // Record a bank that scanning already identified.
  ScanSequenceDraft& useBank(AssetId bank);
  ScanSequenceDraft& useBank(const ScanSoundBankDraft& bank);
  // Choose from scanned banks when collections are rebuilt, including after new files arrive.
  ScanSequenceDraft& useBanks(DependencySelector select);
  // Configure playback from the prepared banks when binding a collection.
  ScanSequenceDraft& prepare(SequencePreparer prepare);

  // Pass the value saved with data(...) to the callback as a const Data& argument.
  template <class Data, class Prepare>
  ScanSequenceDraft& prepare(Prepare prepare);

  template <typename T>
  ScanSequenceDraft& data(T value);

private:
  friend class ScanResultBuilder;

  struct Pending {
    AssetId id;
    std::string name;
    SourceRange range;
    std::optional<SequenceProgram> program;
    AssetPrivateData privateData;
    SequenceCollectionOptions collection;
    std::vector<DependencyRequest> bankRequests;
    SequencePreparer prepare;
  };

  explicit ScanSequenceDraft(Pending& pending) : pending_(&pending) {}
  Pending* pending_;
};

class ScanSoundBankDraft {
public:
  [[nodiscard]] AssetId id() const noexcept { return pending_->id; }

  [[nodiscard]] InstrumentSetBuilder& instruments() { return pending_->instruments; }
  [[nodiscard]] SamplePoolBuilder& localSamples() { return pending_->samples; }
  // Record a known pool, or supply a callback to choose pools when collections are rebuilt.
  ScanSoundBankDraft& useSamples(AssetId samples);
  ScanSoundBankDraft& useSamples(const ScanSamplePoolDraft& samples);
  ScanSoundBankDraft& useSamples(DependencySelector select);
  // Connect the chosen samples to a private bank copy when binding a collection.
  ScanSoundBankDraft& prepare(BankPreparer prepare);

  // Pass the value saved with data(...) to the callback as a const Data& argument.
  template <class Data, class Prepare>
  ScanSoundBankDraft& prepare(Prepare prepare);

  template <typename T>
  ScanSoundBankDraft& data(T value);

private:
  friend class ScanResultBuilder;

  struct Pending {
    AssetId id;
    std::string name;
    InstrumentSetBuilder instruments;
    SamplePoolBuilder samples;
    AssetPrivateData privateData;
    std::vector<DependencyRequest> sampleRequests;
    BankPreparer prepare;
  };

  explicit ScanSoundBankDraft(Pending& pending) : pending_(&pending) {}
  Pending* pending_;
};

class ScanSamplePoolDraft {
public:
  [[nodiscard]] AssetId id() const noexcept { return pending_->id; }

  [[nodiscard]] SamplePoolBuilder& samples() { return pending_->samples; }
  [[nodiscard]] const SamplePoolBuilder& samples() const { return pending_->samples; }

  template <typename T>
  ScanSamplePoolDraft& data(T value);

private:
  friend class ScanResultBuilder;

  struct Pending {
    AssetId id;
    std::string name;
    SamplePoolBuilder samples;
    AssetPrivateData privateData;
  };

  explicit ScanSamplePoolDraft(Pending& pending) : pending_(&pending) {}
  Pending* pending_;
};

// Convenience wrapper for the normal scanner path. It still produces ordinary
// ScanResult values, but keeps format modules away from repetitive ID allocation,
// asset metadata setup, and diagnostics.
class ScanResultBuilder {
public:
  ScanResultBuilder(ScanInput input, std::string format);
  ScanResultBuilder(const ScanResultBuilder&) = delete;
  ScanResultBuilder& operator=(const ScanResultBuilder&) = delete;

  [[nodiscard]] SourceId source() const noexcept { return input_.source.id; }
  [[nodiscard]] const SourceFile& sourceFile() const noexcept { return input_.source; }
  [[nodiscard]] ByteReader reader() const noexcept { return input_.reader; }
  [[nodiscard]] std::string sourceDisplayName() const;
  [[nodiscard]] SourceMapBuilder& sourceMap() noexcept { return sourceMap_; }
  [[nodiscard]] std::vector<Diagnostic>& diagnostics() noexcept { return diagnostics_; }

  // Without an explicit range, finish() spans the sequence's owned source
  // annotations and decoded commands in this input. Container bounds can be
  // supplied here or on the draft to keep that range exact.
  [[nodiscard]] ScanSequenceDraft sequence(std::string name, SourceRange range = {});
  [[nodiscard]] ScanSoundBankDraft soundBank(std::string name, SourceRange range = {});
  [[nodiscard]] ScanSamplePoolDraft samplePool(std::string name, SourceRange range = {});
  [[nodiscard]] AssetId misc(std::string name, SourceRange range);

  void diagnostic(Diagnostic diagnostic);
  void warning(std::string message, SourceRange range);

  [[nodiscard]] ScanResult finish();

private:
  friend class ScanSequenceDraft;
  friend class ScanSoundBankDraft;
  friend class ScanSamplePoolDraft;

  [[nodiscard]] AssetMetadata metadata(AssetId id, std::string name, SourceRange range) const;

  static void setPrivateData(AssetPrivateData& target, AssetPrivateData data);

  ScanInput input_;
  std::string format_;
  std::vector<Diagnostic> diagnostics_;
  SourceMapBuilder sourceMap_;

  using PendingAsset = std::variant<ScanSequenceDraft::Pending, ScanSoundBankDraft::Pending,
                                    ScanSamplePoolDraft::Pending, MiscAsset>;
  // Appending to the deque preserves the values referenced by drafts and
  // domain-builder entries.
  std::deque<PendingAsset> drafts_;
};

template <class Data, class Prepare>
ScanSequenceDraft& ScanSequenceDraft::prepare(Prepare callback) {
  return prepare([callback = std::move(callback)](SequencePreparationContext& context) {
    const auto* data = context.sequence.privateData.template get<Data>();
    if (data == nullptr) {
      context.fail("Sequence is missing its preparation data");
    }
    return callback(context, *data);
  });
}

template <class Data, class Prepare>
ScanSoundBankDraft& ScanSoundBankDraft::prepare(Prepare callback) {
  return prepare([callback = std::move(callback)](BankPreparationContext& context) {
    // Replacing prepared data must not invalidate the callback's source data.
    const auto* data = context.asset.privateData.template get<Data>();
    if (data == nullptr) {
      context.fail("Bank is missing its preparation data");
    }
    callback(context, *data);
  });
}

template <typename T>
ScanSequenceDraft& ScanSequenceDraft::data(T value) {
  ScanResultBuilder::setPrivateData(pending_->privateData, AssetPrivateData::make(std::move(value)));
  return *this;
}

template <typename T>
ScanSoundBankDraft& ScanSoundBankDraft::data(T value) {
  ScanResultBuilder::setPrivateData(pending_->privateData, AssetPrivateData::make(std::move(value)));
  return *this;
}

template <typename T>
ScanSamplePoolDraft& ScanSamplePoolDraft::data(T value) {
  ScanResultBuilder::setPrivateData(pending_->privateData, AssetPrivateData::make(std::move(value)));
  return *this;
}

}  // namespace vgmtrans::core

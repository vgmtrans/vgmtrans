/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/model/SourceMap.h"
#include "value/synth/SynthBuilder.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// Drafts expose these small durable references when another asset needs to keep
// the identity but not the draft's authoring surface.
struct ScanSequenceRef {
  AssetId id;
};

struct ScanInstrumentSetRef {
  AssetId id;
};

struct ScanSampleCollectionRef {
  AssetId id;
};

struct ScanMiscAssetRef {
  AssetId id;
};

struct ScanSynthRefs {
  ScanInstrumentSetRef instruments;
  ScanSampleCollectionRef samples;
};

class ScanResultBuilder;

// Drafts are lightweight views into result-owned pending assets. Creating a
// draft is the publication decision: ScanResultBuilder::finish() materializes
// it even when an instrument set or sample collection remains empty.
class ScanSequenceDraft {
public:
  [[nodiscard]] ScanSequenceRef ref() const noexcept { return ScanSequenceRef{.id = id_}; }
  [[nodiscard]] AssetId id() const noexcept { return id_; }
  ScanSequenceDraft& range(SourceRange range);
  ScanSequenceDraft& program(SequenceProgram program);

  template <typename T>
  ScanSequenceDraft& data(T value);

private:
  friend class ScanResultBuilder;

  ScanSequenceDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

class ScanInstrumentSetDraft {
public:
  [[nodiscard]] ScanInstrumentSetRef ref() const noexcept { return ScanInstrumentSetRef{.id = id_}; }
  [[nodiscard]] AssetId id() const noexcept { return id_; }

  InstrumentSetBuilder::Entry append(Instrument instrument);
  InstrumentSetBuilder::Entry add(u64 groupingKey, Instrument instrument);
  InstrumentSetBuilder::Entry getOrAdd(u64 groupingKey, Instrument initialValue);
  [[nodiscard]] std::optional<InstrumentSetBuilder::Entry> find(u64 groupingKey);

  AnnotationBuilder source(SourceRole role, std::string_view label, SourceRange range, std::string_view kind = {});
  AnnotationBuilder source(SourceRole role, std::string_view label, const SourceRecord& record,
                           std::string_view kind = {});

  ScanInstrumentSetDraft& include(SourceRange range);
  [[nodiscard]] SourceRange range() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] size_t size() const noexcept;

  void warning(std::string message, SourceRange range = {});
  void error(std::string message, SourceRange range = {});

  // Existing reusable synth helpers may operate on the domain builder
  // directly. The draft remains its owner and finish() remains scan-owned.
  [[nodiscard]] InstrumentSetBuilder& builder();

  template <typename T>
  ScanInstrumentSetDraft& data(T value);

private:
  friend class ScanResultBuilder;

  ScanInstrumentSetDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

class ScanSampleCollectionDraft {
public:
  [[nodiscard]] ScanSampleCollectionRef ref() const noexcept { return ScanSampleCollectionRef{.id = id_}; }
  [[nodiscard]] AssetId id() const noexcept { return id_; }

  SampleCollectionBuilder::Entry add(u64 sourceKey, Sample sample);
  SampleCollectionBuilder::Entry alias(u64 aliasKey, u64 existingKey);
  [[nodiscard]] std::optional<SampleRef> find(u64 sourceKey) const;

  AnnotationBuilder source(SourceRole role, std::string_view label, SourceRange range, std::string_view kind = {});
  AnnotationBuilder source(SourceRole role, std::string_view label, const SourceRecord& record,
                           std::string_view kind = {});

  ScanSampleCollectionDraft& include(SourceRange range);
  [[nodiscard]] SourceRange range() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] size_t size() const noexcept;

  void warning(std::string message, SourceRange range = {});
  void error(std::string message, SourceRange range = {});

  [[nodiscard]] SampleCollectionBuilder& builder();
  [[nodiscard]] const SampleCollectionBuilder& builder() const;

  template <typename T>
  ScanSampleCollectionDraft& data(T value);

private:
  friend class ScanResultBuilder;

  ScanSampleCollectionDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

class ScanMiscDraft {
public:
  [[nodiscard]] ScanMiscAssetRef ref() const noexcept { return ScanMiscAssetRef{.id = id_}; }
  [[nodiscard]] AssetId id() const noexcept { return id_; }
  ScanMiscDraft& payload(std::vector<u8> payload);

private:
  friend class ScanResultBuilder;

  ScanMiscDraft(ScanResultBuilder& out, size_t slot, AssetId id);

  ScanResultBuilder* out_ = nullptr;
  size_t slot_ = 0;
  AssetId id_;
};

// Builds one scanner-known collection. This is the common path when a format has
// already discovered the sequence, instruments, and samples together.
class ScanCollectionBuilder {
public:
  ScanCollectionBuilder(ScanResultBuilder& out, size_t index);

  ScanCollectionBuilder& sequence(ScanSequenceRef asset);
  ScanCollectionBuilder& sequence(const ScanSequenceDraft& asset);
  ScanCollectionBuilder& instrumentSet(ScanInstrumentSetRef asset);
  ScanCollectionBuilder& instrumentSet(const ScanInstrumentSetDraft& asset);
  ScanCollectionBuilder& samples(ScanSampleCollectionRef asset);
  ScanCollectionBuilder& samples(const ScanSampleCollectionDraft& asset);
  ScanCollectionBuilder& misc(ScanMiscAssetRef asset);
  ScanCollectionBuilder& misc(const ScanMiscDraft& asset);

private:
  ScanResultBuilder& out_;
  size_t index_ = 0;
};

// Convenience wrapper for the normal scanner path. It still produces ordinary
// ScanResult values, but keeps format modules away from repetitive ID allocation,
// asset metadata setup, diagnostics, and simple collection-member facts.
class ScanResultBuilder {
public:
  ScanResultBuilder(ScanInput input, std::string format);
  ScanResultBuilder(ScanInput input, std::string format, std::string collectionResolver);
  ~ScanResultBuilder();

  [[nodiscard]] SourceId source() const noexcept { return input_.source.id; }
  [[nodiscard]] const SourceFile& sourceFile() const noexcept { return input_.source; }
  [[nodiscard]] ByteReader reader() const noexcept { return input_.reader; }
  [[nodiscard]] std::string_view format() const noexcept { return format_; }
  [[nodiscard]] std::string sourceDisplayName() const;
  [[nodiscard]] SourceMapBuilder& sourceMap() noexcept { return sourceMap_; }
  [[nodiscard]] std::vector<Diagnostic>& diagnostics() noexcept { return result_.diagnostics; }

  [[nodiscard]] ScanSequenceDraft sequence(std::string name, SourceRange range = {});
  [[nodiscard]] ScanInstrumentSetDraft instrumentSet(std::string name, SourceRange range = {});
  [[nodiscard]] ScanSampleCollectionDraft sampleCollection(std::string name, SourceRange range = {});
  [[nodiscard]] ScanMiscDraft misc(std::string name, SourceRange range);

  [[nodiscard]] ScanCollectionBuilder collection(std::string name);
  [[nodiscard]] ScanCollectionBuilder collection(std::string name, CollectionKey key);
  // Use when a scanner produces one collection per source and its display name
  // should not affect collection identity.
  [[nodiscard]] ScanCollectionBuilder sourceCollection(std::string name);

  void fact(AssetId asset, MatchScope scope, MatchFactPayload payload);
  void sourceFact(AssetId asset, MatchFactPayload payload);
  void sessionFact(AssetId asset, MatchFactPayload payload);

  void diagnostic(Diagnostic diagnostic);
  void warning(std::string message, SourceRange range);
  void error(std::string message, SourceRange range);

  [[nodiscard]] ScanResult finish();

private:
  friend class ScanCollectionBuilder;
  friend class ScanSequenceDraft;
  friend class ScanInstrumentSetDraft;
  friend class ScanSampleCollectionDraft;
  friend class ScanMiscDraft;

  enum class DraftRole {
    Sequence,
    InstrumentSet,
    SampleCollection,
    Misc,
  };

  [[nodiscard]] AssetMetadata metadata(AssetId id, std::string name, SourceRange range) const;
  [[nodiscard]] CollectionKey defaultCollectionKey(std::string_view name) const;
  [[nodiscard]] ExplicitCollection& explicitCollection(size_t index);
  [[nodiscard]] static std::string roleName(DraftRole role);

  void validateDraftReference(AssetId id, DraftRole role) const;
  void setSequenceRange(size_t slot, SourceRange range);
  void setSequenceProgram(size_t slot, SequenceProgram program);
  void setPrivateData(size_t slot, AssetPrivateData data);
  void setMiscPayload(size_t slot, std::vector<u8> payload);
  [[nodiscard]] InstrumentSetBuilder& instrumentDraft(size_t slot);
  [[nodiscard]] const InstrumentSetBuilder& instrumentDraft(size_t slot) const;
  [[nodiscard]] SampleCollectionBuilder& sampleDraft(size_t slot);
  [[nodiscard]] const SampleCollectionBuilder& sampleDraft(size_t slot) const;

  ScanInput input_;
  std::string format_;
  std::string collectionResolver_;
  ScanResult result_;
  SourceMapBuilder sourceMap_;

  struct DraftSlot;
  // Domain-builder entries retain pointers to their builders, so each slot has
  // a stable address even while the list of published drafts grows.
  std::vector<std::unique_ptr<DraftSlot>> drafts_;
};

template <typename T>
ScanSequenceDraft& ScanSequenceDraft::data(T value) {
  out_->setPrivateData(slot_, AssetPrivateData::make(std::move(value)));
  return *this;
}

template <typename T>
ScanInstrumentSetDraft& ScanInstrumentSetDraft::data(T value) {
  out_->setPrivateData(slot_, AssetPrivateData::make(std::move(value)));
  return *this;
}

template <typename T>
ScanSampleCollectionDraft& ScanSampleCollectionDraft::data(T value) {
  out_->setPrivateData(slot_, AssetPrivateData::make(std::move(value)));
  return *this;
}

}  // namespace vgmtrans::core

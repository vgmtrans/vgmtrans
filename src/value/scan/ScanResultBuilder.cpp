/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanResultBuilder.h"

#include <filesystem>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string namedSourceCollectionKey(SourceId source, std::string_view name) {
  return "source:" + std::to_string(source.value) + ":collection:" + std::string(name);
}

struct PendingSequence {
  AssetId id;
  std::string name;
  SourceRange range;
  std::optional<SequenceProgram> program;
};

struct PendingInstrumentSet {
  AssetId id;
  std::string name;
  InstrumentSetBuilder instruments;
};

struct PendingSampleCollection {
  AssetId id;
  std::string name;
  SampleCollectionBuilder samples;
};

struct PendingMisc {
  AssetId id;
  std::string name;
  SourceRange range;
  std::optional<std::vector<u8>> payload;
};

}  // namespace

struct ScanResultBuilder::DraftSlot {
  using Value = std::variant<PendingSequence, PendingInstrumentSet, PendingSampleCollection, PendingMisc>;

  explicit DraftSlot(Value value) : value(std::move(value)) {}

  Value value;
};

ScanSequenceDraft::ScanSequenceDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

ScanSequenceDraft& ScanSequenceDraft::range(SourceRange range) {
  out_->setSequenceRange(slot_, range);
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::program(SequenceProgram program) {
  out_->setSequenceProgram(slot_, std::move(program));
  return *this;
}

ScanInstrumentSetDraft::ScanInstrumentSetDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

InstrumentSetBuilder::Entry ScanInstrumentSetDraft::append(Instrument instrument) {
  return builder().append(std::move(instrument));
}

InstrumentSetBuilder::Entry ScanInstrumentSetDraft::add(u64 groupingKey, Instrument instrument) {
  return builder().add(groupingKey, std::move(instrument));
}

InstrumentSetBuilder::Entry ScanInstrumentSetDraft::getOrAdd(u64 groupingKey, Instrument initialValue) {
  return builder().getOrAdd(groupingKey, std::move(initialValue));
}

std::optional<InstrumentSetBuilder::Entry> ScanInstrumentSetDraft::find(u64 groupingKey) {
  return builder().find(groupingKey);
}

AnnotationBuilder ScanInstrumentSetDraft::source(SourceRole role, std::string_view label, SourceRange range,
                                                 std::string_view kind) {
  return builder().source(role, label, range, kind);
}

AnnotationBuilder ScanInstrumentSetDraft::source(SourceRole role, std::string_view label, const SourceRecord& record,
                                                 std::string_view kind) {
  return builder().source(role, label, record, kind);
}

ScanInstrumentSetDraft& ScanInstrumentSetDraft::include(SourceRange range) {
  builder().include(range);
  return *this;
}

SourceRange ScanInstrumentSetDraft::range() const noexcept {
  return out_->instrumentDraft(slot_).range();
}

bool ScanInstrumentSetDraft::empty() const noexcept {
  return out_->instrumentDraft(slot_).empty();
}

size_t ScanInstrumentSetDraft::size() const noexcept {
  return out_->instrumentDraft(slot_).size();
}

void ScanInstrumentSetDraft::warning(std::string message, SourceRange range) {
  builder().warning(std::move(message), range);
}

void ScanInstrumentSetDraft::error(std::string message, SourceRange range) {
  builder().error(std::move(message), range);
}

InstrumentSetBuilder& ScanInstrumentSetDraft::builder() {
  return out_->instrumentDraft(slot_);
}

ScanSampleCollectionDraft::ScanSampleCollectionDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

SampleCollectionBuilder::Entry ScanSampleCollectionDraft::add(u64 sourceKey, Sample sample) {
  return builder().add(sourceKey, std::move(sample));
}

SampleCollectionBuilder::Entry ScanSampleCollectionDraft::alias(u64 aliasKey, u64 existingKey) {
  return builder().alias(aliasKey, existingKey);
}

std::optional<SampleRef> ScanSampleCollectionDraft::find(u64 sourceKey) const {
  return builder().find(sourceKey);
}

AnnotationBuilder ScanSampleCollectionDraft::source(SourceRole role, std::string_view label, SourceRange range,
                                                    std::string_view kind) {
  return builder().source(role, label, range, kind);
}

AnnotationBuilder ScanSampleCollectionDraft::source(SourceRole role, std::string_view label, const SourceRecord& record,
                                                    std::string_view kind) {
  return builder().source(role, label, record, kind);
}

ScanSampleCollectionDraft& ScanSampleCollectionDraft::include(SourceRange range) {
  builder().include(range);
  return *this;
}

SourceRange ScanSampleCollectionDraft::range() const noexcept {
  return builder().range();
}

bool ScanSampleCollectionDraft::empty() const noexcept {
  return builder().empty();
}

size_t ScanSampleCollectionDraft::size() const noexcept {
  return builder().size();
}

void ScanSampleCollectionDraft::warning(std::string message, SourceRange range) {
  builder().warning(std::move(message), range);
}

void ScanSampleCollectionDraft::error(std::string message, SourceRange range) {
  builder().error(std::move(message), range);
}

SampleCollectionBuilder& ScanSampleCollectionDraft::builder() {
  return out_->sampleDraft(slot_);
}

const SampleCollectionBuilder& ScanSampleCollectionDraft::builder() const {
  return out_->sampleDraft(slot_);
}

ScanMiscDraft::ScanMiscDraft(ScanResultBuilder& out, size_t slot, AssetId id) : out_(&out), slot_(slot), id_(id) {
}

ScanMiscDraft& ScanMiscDraft::payload(std::vector<u8> payload) {
  out_->setMiscPayload(slot_, std::move(payload));
  return *this;
}

ScanCollectionBuilder::ScanCollectionBuilder(ScanResultBuilder& out, size_t index) : out_(out), index_(index) {
}

ScanCollectionBuilder& ScanCollectionBuilder::sequence(ScanSequenceRef asset) {
  out_.validateDraftReference(asset.id, ScanResultBuilder::DraftRole::Sequence);
  out_.explicitCollection(index_).members.sequence = asset.id;
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::sequence(const ScanSequenceDraft& asset) {
  return sequence(asset.ref());
}

ScanCollectionBuilder& ScanCollectionBuilder::instrumentSet(ScanInstrumentSetRef asset) {
  out_.validateDraftReference(asset.id, ScanResultBuilder::DraftRole::InstrumentSet);
  out_.explicitCollection(index_).members.instrumentSets.push_back(asset.id);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::instrumentSet(const ScanInstrumentSetDraft& asset) {
  return instrumentSet(asset.ref());
}

ScanCollectionBuilder& ScanCollectionBuilder::samples(ScanSampleCollectionRef asset) {
  out_.validateDraftReference(asset.id, ScanResultBuilder::DraftRole::SampleCollection);
  out_.explicitCollection(index_).members.sampleCollections.push_back(asset.id);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::samples(const ScanSampleCollectionDraft& asset) {
  return samples(asset.ref());
}

ScanCollectionBuilder& ScanCollectionBuilder::misc(ScanMiscAssetRef asset) {
  out_.validateDraftReference(asset.id, ScanResultBuilder::DraftRole::Misc);
  out_.explicitCollection(index_).members.miscAssets.push_back(asset.id);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::misc(const ScanMiscDraft& asset) {
  return misc(asset.ref());
}

ScanResultBuilder::ScanResultBuilder(ScanInput input, std::string format)
    : ScanResultBuilder(std::move(input), std::move(format), {}) {
}

ScanResultBuilder::ScanResultBuilder(ScanInput input, std::string format, std::string collectionResolver)
    : input_(std::move(input)), format_(std::move(format)),
      collectionResolver_(collectionResolver.empty() ? format_ : std::move(collectionResolver)),
      sourceMap_([this]() { return input_.ids.nextSourceAnnotationId(); }) {
}

ScanResultBuilder::~ScanResultBuilder() = default;

std::string ScanResultBuilder::sourceDisplayName() const {
  if (input_.source.title && !input_.source.title->empty()) {
    return *input_.source.title;
  }
  if (!input_.source.name.empty()) {
    return std::filesystem::path(input_.source.name).stem().string();
  }
  if (!input_.source.path.empty()) {
    return input_.source.path.stem().string();
  }
  return format_;
}

ScanSequenceDraft ScanResultBuilder::sequence(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  const size_t slot = drafts_.size();
  drafts_.push_back(std::make_unique<DraftSlot>(PendingSequence{.id = id, .name = std::move(name), .range = range}));
  return ScanSequenceDraft(*this, slot, id);
}

ScanInstrumentSetDraft ScanResultBuilder::instrumentSet(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  const size_t slot = drafts_.size();
  drafts_.push_back(std::make_unique<DraftSlot>(PendingInstrumentSet{
      .id = id,
      .name = std::move(name),
      .instruments = InstrumentSetBuilder{id, &sourceMap_, &result_.diagnostics},
  }));
  ScanInstrumentSetDraft draft(*this, slot, id);
  if (range.valid()) {
    draft.include(range);
  }
  return draft;
}

ScanSampleCollectionDraft ScanResultBuilder::sampleCollection(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  const size_t slot = drafts_.size();
  drafts_.push_back(std::make_unique<DraftSlot>(PendingSampleCollection{
      .id = id,
      .name = std::move(name),
      .samples = SampleCollectionBuilder{id, &sourceMap_, &result_.diagnostics},
  }));
  ScanSampleCollectionDraft draft(*this, slot, id);
  if (range.valid()) {
    draft.include(range);
  }
  return draft;
}

ScanMiscDraft ScanResultBuilder::misc(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  const size_t slot = drafts_.size();
  drafts_.push_back(std::make_unique<DraftSlot>(PendingMisc{.id = id, .name = std::move(name), .range = range}));
  return ScanMiscDraft(*this, slot, id);
}

ScanCollectionBuilder ScanResultBuilder::collection(std::string name) {
  return collection(name, defaultCollectionKey(name));
}

ScanCollectionBuilder ScanResultBuilder::collection(std::string name, CollectionKey key) {
  if (key.resolver.empty()) {
    key.resolver = collectionResolver_;
  }
  if (key.value.empty()) {
    key.value = namedSourceCollectionKey(input_.source.id, name);
  }
  const size_t index = result_.explicitCollections.size();
  result_.explicitCollections.push_back(ExplicitCollection{
      .key = std::move(key),
      .name = std::move(name),
  });
  return ScanCollectionBuilder(*this, index);
}

ScanCollectionBuilder ScanResultBuilder::sourceCollection(std::string name) {
  CollectionKey key{
      .value = "source:" + std::to_string(input_.source.id.value),
  };
  return collection(std::move(name), std::move(key));
}

void ScanResultBuilder::fact(AssetId asset, MatchScope scope, MatchFactPayload payload) {
  result_.matchFacts.push_back(MatchFact{
      .asset = asset,
      .format = format_,
      .scope = std::move(scope),
      .payload = std::move(payload),
  });
}

void ScanResultBuilder::sourceFact(AssetId asset, MatchFactPayload payload) {
  fact(asset, MatchScope{.kind = MatchScopeKind::Source, .source = input_.source.id}, std::move(payload));
}

void ScanResultBuilder::sessionFact(AssetId asset, MatchFactPayload payload) {
  fact(asset, MatchScope{.kind = MatchScopeKind::Session}, std::move(payload));
}

void ScanResultBuilder::diagnostic(Diagnostic diagnostic) {
  result_.diagnostics.push_back(std::move(diagnostic));
}

void ScanResultBuilder::warning(std::string message, SourceRange range) {
  diagnostic(Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range});
}

void ScanResultBuilder::error(std::string message, SourceRange range) {
  diagnostic(Diagnostic{.severity = Severity::Error, .message = std::move(message), .range = range});
}

ScanResult ScanResultBuilder::finish() {
  for (const auto& slot : drafts_) {
    std::visit(
        [](const auto& pending) {
          using Pending = std::decay_t<decltype(pending)>;
          if constexpr (std::is_same_v<Pending, PendingSequence>) {
            if (!pending.program) {
              throw std::logic_error("ScanResultBuilder sequence draft was never given a program");
            }
          } else if constexpr (std::is_same_v<Pending, PendingMisc>) {
            if (!pending.payload) {
              throw std::logic_error("ScanResultBuilder misc draft was never given a payload");
            }
          }
        },
        slot->value);
  }

  result_.assets.reserve(result_.assets.size() + drafts_.size());
  for (auto& slot : drafts_) {
    Asset asset = std::visit(
        [this](auto& pending) -> Asset {
          using Pending = std::decay_t<decltype(pending)>;
          if constexpr (std::is_same_v<Pending, PendingSequence>) {
            return SequenceProgramAsset{
                .metadata = metadata(pending.id, std::move(pending.name), pending.range),
                .program = std::move(*pending.program),
            };
          } else if constexpr (std::is_same_v<Pending, PendingInstrumentSet>) {
            auto built = std::move(pending.instruments).finish();
            return InstrumentSetAsset{
                .metadata = metadata(pending.id, std::move(pending.name), built.range),
                .instruments = std::move(built.values),
            };
          } else if constexpr (std::is_same_v<Pending, PendingSampleCollection>) {
            auto built = std::move(pending.samples).finish();
            return SampleCollectionAsset{
                .metadata = metadata(pending.id, std::move(pending.name), built.range),
                .samples = std::move(built.value),
            };
          } else {
            return MiscAsset{
                .metadata = metadata(pending.id, std::move(pending.name), pending.range),
                .payload = std::move(*pending.payload),
            };
          }
        },
        slot->value);
    result_.assets.push_back(std::move(asset));
  }

  result_.sourceMap = sourceMap_.finish();
  return std::move(result_);
}

AssetMetadata ScanResultBuilder::metadata(AssetId id, std::string name, SourceRange range) const {
  return AssetMetadata{
      .id = id,
      .format = format_,
      .name = std::move(name),
      .range = range,
  };
}

CollectionKey ScanResultBuilder::defaultCollectionKey(std::string_view name) const {
  return CollectionKey{
      .resolver = collectionResolver_,
      .value = namedSourceCollectionKey(input_.source.id, name),
  };
}

ExplicitCollection& ScanResultBuilder::explicitCollection(size_t index) {
  return result_.explicitCollections.at(index);
}

std::string ScanResultBuilder::roleName(DraftRole role) {
  switch (role) {
    case DraftRole::Sequence:
      return "sequence";
    case DraftRole::InstrumentSet:
      return "instrument-set";
    case DraftRole::SampleCollection:
      return "sample-collection";
    case DraftRole::Misc:
      return "misc";
  }
  return "unknown";
}

void ScanResultBuilder::validateDraftReference(AssetId id, DraftRole role) const {
  for (const auto& slot : drafts_) {
    const auto found = std::visit([&](const auto& pending) { return pending.id == id; }, slot->value);
    if (!found) {
      continue;
    }
    const DraftRole actual = std::visit(
        [](const auto& pending) {
          using Pending = std::decay_t<decltype(pending)>;
          if constexpr (std::is_same_v<Pending, PendingSequence>) {
            return DraftRole::Sequence;
          } else if constexpr (std::is_same_v<Pending, PendingInstrumentSet>) {
            return DraftRole::InstrumentSet;
          } else if constexpr (std::is_same_v<Pending, PendingSampleCollection>) {
            return DraftRole::SampleCollection;
          } else {
            return DraftRole::Misc;
          }
        },
        slot->value);
    if (actual != role) {
      throw std::logic_error("ScanResultBuilder collection used " + roleName(actual) + " draft as a " + roleName(role));
    }
    return;
  }
  throw std::logic_error("ScanResultBuilder collection referenced an unknown " + roleName(role) + " asset id " +
                         std::to_string(id.value));
}

void ScanResultBuilder::setSequenceProgram(size_t slot, SequenceProgram program) {
  auto& pending = std::get<PendingSequence>(drafts_.at(slot)->value);
  if (pending.program) {
    throw std::logic_error("ScanResultBuilder sequence draft was given more than one program");
  }
  pending.program = std::move(program);
}

void ScanResultBuilder::setSequenceRange(size_t slot, SourceRange range) {
  std::get<PendingSequence>(drafts_.at(slot)->value).range = range;
}

void ScanResultBuilder::setMiscPayload(size_t slot, std::vector<u8> payload) {
  auto& pending = std::get<PendingMisc>(drafts_.at(slot)->value);
  if (pending.payload) {
    throw std::logic_error("ScanResultBuilder misc draft was given more than one payload");
  }
  pending.payload = std::move(payload);
}

InstrumentSetBuilder& ScanResultBuilder::instrumentDraft(size_t slot) {
  return std::get<PendingInstrumentSet>(drafts_.at(slot)->value).instruments;
}

const InstrumentSetBuilder& ScanResultBuilder::instrumentDraft(size_t slot) const {
  return std::get<PendingInstrumentSet>(drafts_.at(slot)->value).instruments;
}

SampleCollectionBuilder& ScanResultBuilder::sampleDraft(size_t slot) {
  return std::get<PendingSampleCollection>(drafts_.at(slot)->value).samples;
}

const SampleCollectionBuilder& ScanResultBuilder::sampleDraft(size_t slot) const {
  return std::get<PendingSampleCollection>(drafts_.at(slot)->value).samples;
}

}  // namespace vgmtrans::core

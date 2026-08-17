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

struct PendingSoundBank {
  AssetId id;
  std::string name;
  InstrumentSetBuilder instruments;
  SamplePoolBuilder samples;
};

struct PendingSamplePool {
  AssetId id;
  std::string name;
  SamplePoolBuilder samples;
};

struct PendingMisc {
  AssetId id;
  std::string name;
  SourceRange range;
  std::optional<std::vector<u8>> payload;
};

}  // namespace

struct ScanResultBuilder::DraftSlot {
  using Value = std::variant<PendingSequence, PendingSoundBank, PendingSamplePool, PendingMisc>;

  explicit DraftSlot(Value value) : value(std::move(value)) {}

  Value value;
  AssetPrivateData privateData;
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

ScanSoundBankDraft::ScanSoundBankDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

InstrumentSetBuilder::Entry ScanSoundBankDraft::append(Instrument instrument) {
  return builder().append(std::move(instrument));
}

InstrumentSetBuilder::Entry ScanSoundBankDraft::add(u64 groupingKey, Instrument instrument) {
  return builder().add(groupingKey, std::move(instrument));
}

InstrumentSetBuilder::Entry ScanSoundBankDraft::getOrAdd(u64 groupingKey, Instrument initialValue) {
  return builder().getOrAdd(groupingKey, std::move(initialValue));
}

std::optional<InstrumentSetBuilder::Entry> ScanSoundBankDraft::find(u64 groupingKey) {
  return builder().find(groupingKey);
}

AnnotationBuilder ScanSoundBankDraft::source(SourceRole role, std::string_view label, SourceRange range,
                                             std::string_view kind) {
  return builder().source(role, label, range, kind);
}

AnnotationBuilder ScanSoundBankDraft::source(SourceRole role, std::string_view label, const SourceRecord& record,
                                             std::string_view kind) {
  return builder().source(role, label, record, kind);
}

ScanSoundBankDraft& ScanSoundBankDraft::include(SourceRange range) {
  builder().include(range);
  return *this;
}

SourceRange ScanSoundBankDraft::range() const noexcept {
  return out_->instrumentDraft(slot_).range();
}

bool ScanSoundBankDraft::empty() const noexcept {
  return out_->instrumentDraft(slot_).empty();
}

void ScanSoundBankDraft::warning(std::string message, SourceRange range) {
  builder().warning(std::move(message), range);
}

InstrumentSetBuilder& ScanSoundBankDraft::builder() {
  return out_->instrumentDraft(slot_);
}

SamplePoolBuilder& ScanSoundBankDraft::samples() {
  return out_->localSampleDraft(slot_);
}

ScanSamplePoolDraft::ScanSamplePoolDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

SamplePoolBuilder::Entry ScanSamplePoolDraft::add(u64 sourceKey, Sample sample) {
  return builder().add(sourceKey, std::move(sample));
}

SamplePoolBuilder::Entry ScanSamplePoolDraft::alias(u64 aliasKey, u64 existingKey) {
  return builder().alias(aliasKey, existingKey);
}

std::optional<SampleRef> ScanSamplePoolDraft::find(u64 sourceKey) const {
  return builder().find(sourceKey);
}

AnnotationBuilder ScanSamplePoolDraft::source(SourceRole role, std::string_view label, SourceRange range,
                                              std::string_view kind) {
  return builder().source(role, label, range, kind);
}

AnnotationBuilder ScanSamplePoolDraft::source(SourceRole role, std::string_view label, const SourceRecord& record,
                                              std::string_view kind) {
  return builder().source(role, label, record, kind);
}

ScanSamplePoolDraft& ScanSamplePoolDraft::include(SourceRange range) {
  builder().include(range);
  return *this;
}

void ScanSamplePoolDraft::warning(std::string message, SourceRange range) {
  builder().warning(std::move(message), range);
}

SamplePoolBuilder& ScanSamplePoolDraft::builder() {
  return out_->sampleDraft(slot_);
}

const SamplePoolBuilder& ScanSamplePoolDraft::builder() const {
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

ScanCollectionBuilder& ScanCollectionBuilder::soundBank(ScanSoundBankRef asset) {
  out_.validateDraftReference(asset.id, ScanResultBuilder::DraftRole::SoundBank);
  out_.explicitCollection(index_).members.soundBanks.push_back(asset.id);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::soundBank(const ScanSoundBankDraft& asset) {
  return soundBank(asset.ref());
}

ScanCollectionBuilder& ScanCollectionBuilder::samplePool(ScanSamplePoolRef asset) {
  out_.validateDraftReference(asset.id, ScanResultBuilder::DraftRole::SamplePool);
  out_.explicitCollection(index_).members.samplePools.push_back(asset.id);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::samplePool(const ScanSamplePoolDraft& asset) {
  return samplePool(asset.ref());
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

ScanSoundBankDraft ScanResultBuilder::soundBank(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  const size_t slot = drafts_.size();
  drafts_.push_back(std::make_unique<DraftSlot>(PendingSoundBank{
      .id = id,
      .name = std::move(name),
      .instruments = InstrumentSetBuilder{id, &sourceMap_, &result_.diagnostics},
      .samples = SamplePoolBuilder{id, &sourceMap_, &result_.diagnostics},
  }));
  ScanSoundBankDraft draft(*this, slot, id);
  if (range.valid()) {
    draft.include(range);
  }
  return draft;
}

ScanSamplePoolDraft ScanResultBuilder::samplePool(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  const size_t slot = drafts_.size();
  drafts_.push_back(std::make_unique<DraftSlot>(PendingSamplePool{
      .id = id,
      .name = std::move(name),
      .samples = SamplePoolBuilder{id, &sourceMap_, &result_.diagnostics},
  }));
  ScanSamplePoolDraft draft(*this, slot, id);
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
    auto privateData = std::move(slot->privateData);
    Asset asset = std::visit(
        [this, &privateData](auto& pending) -> Asset {
          using Pending = std::decay_t<decltype(pending)>;
          if constexpr (std::is_same_v<Pending, PendingSequence>) {
            return SequenceProgramAsset{
                .metadata = metadata(pending.id, std::move(pending.name), pending.range),
                .program = std::move(*pending.program),
                .privateData = std::move(privateData),
            };
          } else if constexpr (std::is_same_v<Pending, PendingSoundBank>) {
            auto instruments = std::move(pending.instruments).finish();
            auto samples = std::move(pending.samples).finish();
            const SourceRange primaryRange = instruments.range.valid() ? instruments.range : samples.range;
            return SoundBankAsset{
                .metadata = metadata(pending.id, std::move(pending.name), primaryRange),
                .instruments = std::move(instruments.values),
                .localSamples = std::move(samples.value),
                .privateData = std::move(privateData),
            };
          } else if constexpr (std::is_same_v<Pending, PendingSamplePool>) {
            auto built = std::move(pending.samples).finish();
            return SamplePoolAsset{
                .metadata = metadata(pending.id, std::move(pending.name), built.range),
                .pool = std::move(built.value),
                .privateData = std::move(privateData),
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

void ScanResultBuilder::setPrivateData(size_t slot, AssetPrivateData data) {
  auto& privateData = drafts_.at(slot)->privateData;
  if (!privateData.empty()) {
    throw std::logic_error("ScanResultBuilder asset draft was given more than one private data value");
  }
  privateData = std::move(data);
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
    case DraftRole::SoundBank:
      return "sound-bank";
    case DraftRole::SamplePool:
      return "sample-pool";
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
          } else if constexpr (std::is_same_v<Pending, PendingSoundBank>) {
            return DraftRole::SoundBank;
          } else if constexpr (std::is_same_v<Pending, PendingSamplePool>) {
            return DraftRole::SamplePool;
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
  return std::get<PendingSoundBank>(drafts_.at(slot)->value).instruments;
}

const InstrumentSetBuilder& ScanResultBuilder::instrumentDraft(size_t slot) const {
  return std::get<PendingSoundBank>(drafts_.at(slot)->value).instruments;
}

SamplePoolBuilder& ScanResultBuilder::localSampleDraft(size_t slot) {
  return std::get<PendingSoundBank>(drafts_.at(slot)->value).samples;
}

SamplePoolBuilder& ScanResultBuilder::sampleDraft(size_t slot) {
  return std::get<PendingSamplePool>(drafts_.at(slot)->value).samples;
}

const SamplePoolBuilder& ScanResultBuilder::sampleDraft(size_t slot) const {
  return std::get<PendingSamplePool>(drafts_.at(slot)->value).samples;
}

}  // namespace vgmtrans::core

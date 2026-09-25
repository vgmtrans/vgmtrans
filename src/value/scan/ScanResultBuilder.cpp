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
  SequenceCollectionOptions collection;
  SequenceRecipe sequenceRecipe;
  BankRecipe bankRecipe;
  SequencePreparer sequencePreparer;
  BankPreparer bankPreparer;
};

ScanSequenceDraft::ScanSequenceDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

ScanSequenceDraft& ScanSequenceDraft::range(SourceRange range) {
  std::get<PendingSequence>(out_->drafts_.at(slot_)->value).range = range;
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::program(SequenceProgram program) {
  auto& pending = std::get<PendingSequence>(out_->drafts_.at(slot_)->value);
  if (pending.program) {
    throw std::logic_error("ScanResultBuilder sequence draft was given more than one program");
  }
  pending.program = std::move(program);
  return *this;
}

ScanSoundBankDraft::ScanSoundBankDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

ScanSequenceDraft& ScanSequenceDraft::useBank(AssetId bank) {
  out_->drafts_.at(slot_)->sequenceRecipe.banks.emplace_back(DependencyTarget{bank, {}});
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::useBank(const ScanSoundBankDraft& bank) {
  return useBank(bank.id());
}

ScanSequenceDraft& ScanSequenceDraft::useBanks(DependencySelector select) {
  if (!select) {
    throw std::invalid_argument("Bank request has no selector");
  }
  out_->drafts_.at(slot_)->sequenceRecipe.banks.emplace_back(std::move(select));
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::prepare(SequencePreparer prepare) {
  out_->setSequencePreparer(slot_, std::move(prepare));
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::assignBanks(BankAssigner assign) {
  out_->drafts_.at(slot_)->sequenceRecipe.assignBanks = std::move(assign);
  return *this;
}

ScanSoundBankDraft& ScanSoundBankDraft::useSamples(AssetId samples) {
  out_->drafts_.at(slot_)->bankRecipe.samples.emplace_back(DependencyTarget{samples, {}});
  return *this;
}

ScanSoundBankDraft& ScanSoundBankDraft::useSamples(const ScanSamplePoolDraft& samples) {
  return useSamples(samples.id());
}

ScanSoundBankDraft& ScanSoundBankDraft::useSamples(DependencySelector select) {
  if (!select) {
    throw std::invalid_argument("Sample request has no selector");
  }
  out_->drafts_.at(slot_)->bankRecipe.samples.emplace_back(std::move(select));
  return *this;
}

ScanSoundBankDraft& ScanSoundBankDraft::prepare(BankPreparer prepare) {
  out_->setBankPreparer(slot_, std::move(prepare));
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::collectionName(std::string name) {
  out_->drafts_.at(slot_)->collection.name = std::move(name);
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::withoutCollection() {
  out_->drafts_.at(slot_)->collection.enabled = false;
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::includeMisc(AssetId asset) {
  auto& assets = out_->drafts_.at(slot_)->collection.miscAssets;
  if (std::ranges::find(assets, asset) == assets.end()) {
    assets.push_back(asset);
  }
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::includeMisc(const ScanMiscDraft& asset) {
  return includeMisc(asset.id());
}

void ScanResultBuilder::setSequencePreparer(size_t slot, SequencePreparer prepare) {
  drafts_.at(slot)->sequencePreparer = std::move(prepare);
}

void ScanResultBuilder::setBankPreparer(size_t slot, BankPreparer prepare) {
  drafts_.at(slot)->bankPreparer = std::move(prepare);
}

InstrumentSetBuilder& ScanSoundBankDraft::instruments() {
  return std::get<PendingSoundBank>(out_->drafts_.at(slot_)->value).instruments;
}

SamplePoolBuilder& ScanSoundBankDraft::localSamples() {
  return std::get<PendingSoundBank>(out_->drafts_.at(slot_)->value).samples;
}

ScanSamplePoolDraft::ScanSamplePoolDraft(ScanResultBuilder& out, size_t slot, AssetId id)
    : out_(&out), slot_(slot), id_(id) {
}

SamplePoolBuilder& ScanSamplePoolDraft::samples() {
  return std::get<PendingSamplePool>(out_->drafts_.at(slot_)->value).samples;
}

const SamplePoolBuilder& ScanSamplePoolDraft::samples() const {
  return std::get<PendingSamplePool>(out_->drafts_.at(slot_)->value).samples;
}

ScanMiscDraft::ScanMiscDraft(ScanResultBuilder& out, size_t slot, AssetId id) : out_(&out), slot_(slot), id_(id) {
}

ScanMiscDraft& ScanMiscDraft::payload(std::vector<u8> payload) {
  auto& pending = std::get<PendingMisc>(out_->drafts_.at(slot_)->value);
  if (pending.payload) {
    throw std::logic_error("ScanResultBuilder misc draft was given more than one payload");
  }
  pending.payload = std::move(payload);
  return *this;
}

ScanResultBuilder::ScanResultBuilder(ScanInput input, std::string format)
    : input_(std::move(input)), format_(std::move(format)),
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
  draft.instruments().include(range);
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
  draft.samples().include(range);
  return draft;
}

ScanMiscDraft ScanResultBuilder::misc(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  const size_t slot = drafts_.size();
  drafts_.push_back(std::make_unique<DraftSlot>(PendingMisc{.id = id, .name = std::move(name), .range = range}));
  return ScanMiscDraft(*this, slot, id);
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
    if (const auto* sequence = std::get_if<PendingSequence>(&slot->value); sequence && !sequence->program) {
      throw std::logic_error("ScanResultBuilder sequence draft was never given a program");
    }
    if (const auto* misc = std::get_if<PendingMisc>(&slot->value); misc && !misc->payload) {
      throw std::logic_error("ScanResultBuilder misc draft was never given a payload");
    }
  }

  result_.assets.reserve(result_.assets.size() + drafts_.size());
  for (auto& slot : drafts_) {
    auto privateData = std::move(slot->privateData);
    Asset asset = std::visit(
        [this, &privateData, &slot](auto& pending) -> Asset {
          using Pending = std::decay_t<decltype(pending)>;
          if constexpr (std::is_same_v<Pending, PendingSequence>) {
            return SequenceProgramAsset{
                .metadata = metadata(pending.id, std::move(pending.name), pending.range),
                .program = std::move(*pending.program),
                .privateData = std::move(privateData),
                .collection = std::move(slot->collection),
                .recipe = std::move(slot->sequenceRecipe),
                .prepare = std::move(slot->sequencePreparer),
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
                .recipe = std::move(slot->bankRecipe),
                .prepare = std::move(slot->bankPreparer),
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
                .privateData = std::move(privateData),
            };
          }
        },
        slot->value);
    result_.assets.push_back(std::move(asset));
  }

  // Synth drafts may add annotations while finishing, so infer bounds only
  // after all assets and their source metadata have been materialized.
  result_.sourceMap = sourceMap_.finish();
  for (auto& asset : result_.assets) {
    auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || sequence->metadata.range.valid()) {
      continue;
    }
    auto& range = sequence->metadata.range;
    for (const auto id : result_.sourceMap.annotationsForAsset(sequence->metadata.id)) {
      const auto annotated = result_.sourceMap.get(id).range;
      if (annotated.source == source()) {
        range.include(annotated);
      }
    }
    range = sequenceSourceRange(reader(), range, sequence->program);
  }
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

}  // namespace vgmtrans::core

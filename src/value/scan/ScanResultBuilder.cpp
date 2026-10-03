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

ScanSequenceDraft& ScanSequenceDraft::range(SourceRange range) {
  pending_->range = range;
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::program(SequenceProgram program) {
  if (pending_->program) {
    throw std::logic_error("ScanResultBuilder sequence draft was given more than one program");
  }
  pending_->program = std::move(program);
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::useBank(AssetId bank) {
  pending_->bankRequests.emplace_back(DependencyTarget{bank, {}});
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::useBank(const ScanSoundBankDraft& bank) {
  return useBank(bank.id());
}

ScanSequenceDraft& ScanSequenceDraft::useBanks(DependencySelector select) {
  if (!select) {
    throw std::invalid_argument("Bank request has no selector");
  }
  pending_->bankRequests.emplace_back(std::move(select));
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::prepare(SequencePreparer prepare) {
  pending_->prepare = std::move(prepare);
  return *this;
}

ScanSoundBankDraft& ScanSoundBankDraft::useSamples(AssetId samples) {
  pending_->sampleRequests.emplace_back(DependencyTarget{samples, {}});
  return *this;
}

ScanSoundBankDraft& ScanSoundBankDraft::useSamples(const ScanSamplePoolDraft& samples) {
  return useSamples(samples.id());
}

ScanSoundBankDraft& ScanSoundBankDraft::useSamples(DependencySelector select) {
  if (!select) {
    throw std::invalid_argument("Sample request has no selector");
  }
  pending_->sampleRequests.emplace_back(std::move(select));
  return *this;
}

ScanSoundBankDraft& ScanSoundBankDraft::prepare(BankPreparer prepare) {
  pending_->prepare = std::move(prepare);
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::collectionName(std::string name) {
  pending_->collection.name = std::move(name);
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::withoutCollection() {
  pending_->collection.enabled = false;
  return *this;
}

ScanSequenceDraft& ScanSequenceDraft::includeMisc(AssetId asset) {
  auto& assets = pending_->collection.miscAssets;
  if (std::ranges::find(assets, asset) == assets.end()) {
    assets.push_back(asset);
  }
  return *this;
}

ScanResultBuilder::ScanResultBuilder(ScanInput input, std::string format)
    : input_(std::move(input)), format_(std::move(format)),
      sourceMap_([this]() { return input_.ids.nextSourceAnnotationId(); }) {
}

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
  drafts_.emplace_back(ScanSequenceDraft::Pending{.id = id, .name = std::move(name), .range = range});
  return ScanSequenceDraft{std::get<ScanSequenceDraft::Pending>(drafts_.back())};
}

ScanSoundBankDraft ScanResultBuilder::soundBank(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  drafts_.emplace_back(ScanSoundBankDraft::Pending{
      .id = id,
      .name = std::move(name),
      .instruments = InstrumentSetBuilder{id, &sourceMap_, &diagnostics_},
      .samples = SamplePoolBuilder{id, &sourceMap_, &diagnostics_},
  });
  ScanSoundBankDraft draft{std::get<ScanSoundBankDraft::Pending>(drafts_.back())};
  draft.instruments().include(range);
  return draft;
}

ScanSamplePoolDraft ScanResultBuilder::samplePool(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  drafts_.emplace_back(ScanSamplePoolDraft::Pending{
      .id = id,
      .name = std::move(name),
      .samples = SamplePoolBuilder{id, &sourceMap_, &diagnostics_},
  });
  ScanSamplePoolDraft draft{std::get<ScanSamplePoolDraft::Pending>(drafts_.back())};
  draft.samples().include(range);
  return draft;
}

AssetId ScanResultBuilder::misc(std::string name, SourceRange range) {
  const AssetId id = input_.ids.nextAssetId();
  drafts_.emplace_back(MiscAsset{.metadata = metadata(id, std::move(name), range)});
  return id;
}

void ScanResultBuilder::warning(std::string message, SourceRange range) {
  diagnostics_.push_back(Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range});
}

ScanResult ScanResultBuilder::finish() {
  for (const auto& draft : drafts_) {
    if (const auto* sequence = std::get_if<ScanSequenceDraft::Pending>(&draft); sequence && !sequence->program) {
      throw std::logic_error("ScanResultBuilder sequence draft was never given a program");
    }
  }

  ScanResult result;
  result.assets.reserve(drafts_.size());
  for (auto& draft : drafts_) {
    Asset asset = std::visit(
        [this](auto& pending) -> Asset {
          using Pending = std::decay_t<decltype(pending)>;
          if constexpr (std::is_same_v<Pending, ScanSequenceDraft::Pending>) {
            return SequenceProgramAsset{
                .metadata = metadata(pending.id, std::move(pending.name), pending.range),
                .program = std::move(*pending.program),
                .privateData = std::move(pending.privateData),
                .collection = std::move(pending.collection),
                .bankRequests = std::move(pending.bankRequests),
                .prepare = std::move(pending.prepare),
            };
          } else if constexpr (std::is_same_v<Pending, ScanSoundBankDraft::Pending>) {
            auto instruments = std::move(pending.instruments).finish();
            auto samples = std::move(pending.samples).finish();
            const SourceRange instrumentRange = pending.instruments.range();
            const SourceRange primaryRange = instrumentRange.valid() ? instrumentRange : pending.samples.range();
            return SoundBankAsset{
                .metadata = metadata(pending.id, std::move(pending.name), primaryRange),
                .instruments = std::move(instruments),
                .localSamples = std::move(samples),
                .privateData = std::move(pending.privateData),
                .sampleRequests = std::move(pending.sampleRequests),
                .prepare = std::move(pending.prepare),
            };
          } else if constexpr (std::is_same_v<Pending, ScanSamplePoolDraft::Pending>) {
            auto pool = std::move(pending.samples).finish();
            return SamplePoolAsset{
                .metadata = metadata(pending.id, std::move(pending.name), pending.samples.range()),
                .pool = std::move(pool),
                .privateData = std::move(pending.privateData),
            };
          } else {
            return std::move(pending);
          }
        },
        draft);
    result.assets.push_back(std::move(asset));
  }

  // Synth drafts may add annotations while finishing, so infer bounds only
  // after all assets and their source metadata have been materialized.
  result.sourceMap = sourceMap_.finish();
  for (auto& asset : result.assets) {
    auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || sequence->metadata.range.valid()) {
      continue;
    }
    auto& range = sequence->metadata.range;
    for (const auto id : result.sourceMap.annotationsForAsset(sequence->metadata.id)) {
      const auto annotated = result.sourceMap.get(id).range;
      if (annotated.source == source()) {
        range.include(annotated);
      }
    }
    range = sequenceSourceRange(reader(), range, sequence->program);
  }
  result.diagnostics = std::move(diagnostics_);
  return result;
}

void ScanResultBuilder::setPrivateData(AssetPrivateData& target, AssetPrivateData data) {
  if (!target.empty()) {
    throw std::logic_error("ScanResultBuilder asset draft was given more than one private data value");
  }
  target = std::move(data);
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

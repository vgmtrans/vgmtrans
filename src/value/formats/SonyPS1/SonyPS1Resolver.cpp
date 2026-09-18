/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/scan/CollectionDiscovery.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

using InstrumentEntry = AssetWithData<SoundBankAsset, SonyPs1BankLayout>;
using SampleEntry = AssetWithData<SamplePoolAsset, SonyPs1SampleBodyLayout>;

struct SonySampleBinding {
  AssetId soundBank;
  AssetId samplePool;
  u32 firstSample = 0;
};

[[nodiscard]] bool needsExternalSamples(const SoundBankAsset& bank) {
  return std::ranges::any_of(bank.instruments, [](const Instrument& instrument) {
    return std::ranges::any_of(instrument.regions, [](const Region& region) { return region.sample.needsBinding(); });
  });
}

[[nodiscard]] std::filesystem::path sourcePath(const SourceFile* source) {
  if (source == nullptr) {
    return {};
  }
  return source->path.empty() ? std::filesystem::path(source->name) : source->path;
}

[[nodiscard]] bool sameDirectory(const SourceFile* left, const SourceFile* right) {
  const auto a = sourcePath(left);
  const auto b = sourcePath(right);
  return !a.empty() && !b.empty() && a.parent_path() == b.parent_path();
}

[[nodiscard]] bool sameStem(const SourceFile* left, const SourceFile* right) {
  const auto a = sourcePath(left);
  const auto b = sourcePath(right);
  return !a.empty() && !b.empty() && a.parent_path() == b.parent_path() && a.stem() == b.stem();
}

[[nodiscard]] std::vector<const InstrumentEntry*> chooseInstruments(
    const SequenceProgramAsset& sequence, const SourceFile* sequenceFile,
    const std::vector<const SequenceProgramAsset*>& sequenceEntries, const std::vector<InstrumentEntry>& banks) {
  const SourceId source = sequence.metadata.range.source;
  const u32 offset = static_cast<u32>(sequence.metadata.range.offset);
  std::vector<const InstrumentEntry*> selected;
  for (const auto& bank : banks) {
    if (source.valid() && source == bank.sourceId()) {
      selected.push_back(&bank);
    }
  }
  if (!selected.empty()) {
    std::ranges::sort(selected, [](const InstrumentEntry* left, const InstrumentEntry* right) {
      return left->asset->metadata.range.offset > right->asset->metadata.range.offset;
    });
    const size_t rank = std::ranges::count_if(sequenceEntries, [&](const SequenceProgramAsset* candidate) {
      return candidate->metadata.range.source == source && static_cast<u32>(candidate->metadata.range.offset) > offset;
    });
    return {selected[std::min(rank, selected.size() - 1)]};
  }
  for (const auto& bank : banks) {
    if (sameDirectory(sequenceFile, bank.source)) {
      selected.push_back(&bank);
    }
  }
  if (selected.empty() && banks.size() == 1) {
    selected.push_back(&banks.front());
  }
  return selected;
}

[[nodiscard]] std::vector<const SampleEntry*> chooseSamples(const InstrumentEntry& bank,
                                                            const std::vector<SampleEntry>& bodies) {
  return bestMatches(bodies, [&](const SampleEntry& body) {
    if (body.data->length != bank.data->expectedSampleBytes) {
      return -1;
    }
    // Compare successively weaker stem, source, and directory evidence.
    return (sameStem(bank.source, body.source) ? 4 : 0) +
           (bank.sourceId().valid() && bank.sourceId() == body.sourceId() ? 2 : 0) +
           (sameDirectory(bank.source, body.source) ? 1 : 0);
  });
}

void attachBank(CollectionAssembly& collection, const InstrumentEntry& bank, const std::vector<SampleEntry>& bodies,
                std::vector<SonySampleBinding>& bindings) {
  collection.soundBank(bank.id());
  if (!needsExternalSamples(*bank.asset)) {
    return;
  }
  const auto candidates = chooseSamples(bank, bodies);
  if (candidates.size() == 1) {
    collection.samplePool(candidates.front()->id());
    bindings.push_back(SonySampleBinding{.soundBank = bank.id(), .samplePool = candidates.front()->id()});
    return;
  }
  if (!candidates.empty()) {
    collection.ambiguous("Sony PS1 sound bank matches multiple external sample pools", bank.id());
    return;
  }
  collection.incomplete(CollectionIssue{
      .severity = Severity::Warning,
      .code = "missing-sample-pool",
      .message = "Sony PS1 sound bank has no matching external sample pool",
      .asset = bank.id(),
  });
}

void applySampleBinding(CollectionBindingContext& context, SoundBankAsset& bank, const SamplePoolAsset& pool,
                        u32 firstSample) {
  const auto& sizes = bank.privateData.get<SonyPs1BankLayout>()->sampleSizes;
  for (auto& instrument : bank.instruments) {
    for (auto& region : instrument.regions) {
      if (!region.sample.needsBinding()) {
        continue;
      }
      const u32 index = region.sample.index();
      if (index >= sizes.size() || sizes[index] == 0) {
        context.fail("Sony PS1 sound bank refers outside its external sample pool", region.range);
        return;
      }
      const u32 sample = firstSample + index - static_cast<u32>(std::count(sizes.begin(), sizes.begin() + index, 0));
      if (sample >= pool.pool.samples.size()) {
        context.fail("Sony PS1 sound bank refers outside its external sample pool", region.range);
        return;
      }
      region.sample = SampleRef::resolved(pool.metadata.id, sample);
    }
  }
}

void applySonyPs1Bindings(CollectionBindingContext& context, std::span<const SonySampleBinding> bindings) {
  u32 bankNumber = 0;
  for (auto& bank : context.soundBanks) {
    if (bank.metadata.format != kSonyPs1FormatName) {
      continue;
    }
    for (auto& instrument : bank.instruments) {
      const u32 program = instrument.explicitAddress ? instrument.explicitAddress->program
                          : instrument.identity      ? instrument.identity->key & 0xff
                                                     : 0;
      instrument.explicitAddress = InstrumentAddress{.bank = bankNumber, .program = program};
      instrument.identity = sonyPs1InstrumentIdentity(static_cast<u16>(bankNumber), static_cast<u8>(program));
    }
    ++bankNumber;
  }

  for (const auto& binding : bindings) {
    auto* bank = context.soundBank(binding.soundBank);
    const auto* pool = context.samplePool(binding.samplePool);
    if (bank == nullptr || bank->metadata.format != kSonyPs1FormatName) {
      context.fail("Sony PS1 sample binding refers to a missing sound bank");
      return;
    }
    if (pool == nullptr || pool->metadata.format != kSonyPs1FormatName) {
      context.fail("Sony PS1 sample binding refers to a missing sample pool", bank->metadata.range);
      return;
    }
    applySampleBinding(context, *bank, *pool, binding.firstSample);
    if (context.failed) {
      return;
    }
  }

  for (const auto& bank : context.soundBanks) {
    if (bank.metadata.format == kSonyPs1FormatName && needsExternalSamples(bank)) {
      context.fail("Sony PS1 sound bank has no matching external sample pool", bank.metadata.range);
      return;
    }
  }
}

[[nodiscard]] CollectionBinder sonyPs1Binder(std::vector<SonySampleBinding> bindings) {
  return
      [bindings = std::move(bindings)](CollectionBindingContext& context) { applySonyPs1Bindings(context, bindings); };
}

}  // namespace

std::vector<DesiredCollection> resolveSonyPs1Collections(const CollectionDiscoveryContext& context) {
  const auto sequenceEntries = context.assets<SequenceProgramAsset>(kSonyPs1FormatName);
  const auto instrumentEntries = context.assetsWithData<SoundBankAsset, SonyPs1BankLayout>();
  const auto sampleEntries = context.assetsWithData<SamplePoolAsset, SonyPs1SampleBodyLayout>();
  std::vector<DesiredCollection> collections;

  for (const auto* sequence : sequenceEntries) {
    const auto& metadata = sequence->metadata;
    const SourceId source = metadata.range.source;
    CollectionAssembly collection("source:" + std::to_string(source.valid() ? source.value : 0) +
                                      ":sequence:" + std::to_string(static_cast<u32>(metadata.range.offset)),
                                  metadata.name);
    collection.sequence(metadata.id);
    std::vector<SonySampleBinding> bindings;
    const auto banks = chooseInstruments(*sequence, context.sourceFor(metadata), sequenceEntries, instrumentEntries);
    for (const auto* bank : banks) {
      attachBank(collection, *bank, sampleEntries, bindings);
    }
    if (banks.empty()) {
      collection.requireSoundBank();
    }
    collection.bind(sonyPs1Binder(std::move(bindings)));
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindSonyPs1Collection(CollectionBindingContext& context) {
  std::vector<SonySampleBinding> bindings;
  for (const auto& bank : context.soundBanks) {
    if (bank.metadata.format != kSonyPs1FormatName || !needsExternalSamples(bank)) {
      continue;
    }
    const auto* bankData = bank.privateData.get<SonyPs1BankLayout>();
    if (bankData == nullptr) {
      context.fail("Sony PS1 sound bank has no retained external sample size", bank.metadata.range);
      return;
    }
    const SamplePoolAsset* selected = nullptr;
    u32 firstSample = 0;
    for (const auto* pool : context.samplePools) {
      const auto* poolData = pool->privateData.get<SonyPs1SampleBodyLayout>();
      if (pool->metadata.format != kSonyPs1FormatName || poolData == nullptr) {
        continue;
      }
      const auto starts = findSonyPs1SampleStarts(*poolData, bankData->sampleSizes);
      if (starts.empty()) {
        continue;
      }
      if (selected != nullptr || starts.size() > 1) {
        context.fail("Sony PS1 sound bank matches multiple external sample pools or positions", bank.metadata.range);
        return;
      }
      selected = pool;
      firstSample = starts.front();
    }
    if (selected == nullptr) {
      context.fail("Sony PS1 sound bank has no matching external sample pool", bank.metadata.range);
      return;
    }
    bindings.push_back(SonySampleBinding{
        .soundBank = bank.metadata.id, .samplePool = selected->metadata.id, .firstSample = firstSample});
  }
  applySonyPs1Bindings(context, bindings);
}

}  // namespace vgmtrans::formats::sony_ps1

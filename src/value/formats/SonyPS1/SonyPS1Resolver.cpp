/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/scan/AssetResolution.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

using InstrumentEntry = AssetWithData<SoundBankAsset, SonyPs1BankLayout>;
using SampleEntry = AssetWithData<SamplePoolAsset, SonyPs1SampleBodyLayout>;

struct SamplePosition {
  u32 firstSample = 0;
};

[[nodiscard]] bool needsExternalSamples(const SoundBankAsset& bank) {
  return std::ranges::any_of(bank.instruments, [](const Instrument& instrument) {
    return std::ranges::any_of(instrument.regions, [](const Region& region) { return region.sample.needsBinding(); });
  });
}

[[nodiscard]] std::vector<InstrumentEntry> chooseInstruments(
    const SequenceProgramAsset& sequence, const SourceFile* sequenceFile,
    const std::vector<const SequenceProgramAsset*>& sequenceEntries, const std::vector<InstrumentEntry>& banks) {
  const SourceId source = sequence.metadata.range.source;
  const u32 offset = static_cast<u32>(sequence.metadata.range.offset);
  std::vector<InstrumentEntry> selected;
  for (const auto& bank : banks) {
    if (source.valid() && source == bank.sourceId()) {
      selected.push_back(bank);
    }
  }
  if (!selected.empty()) {
    std::ranges::sort(selected, [](const InstrumentEntry& left, const InstrumentEntry& right) {
      return left.asset->metadata.range.offset > right.asset->metadata.range.offset;
    });
    const size_t rank = std::ranges::count_if(sequenceEntries, [&](const SequenceProgramAsset* candidate) {
      return candidate->metadata.range.source == source && static_cast<u32>(candidate->metadata.range.offset) > offset;
    });
    return {selected[std::min(rank, selected.size() - 1)]};
  }
  for (const auto& bank : banks) {
    if (sameDirectory(sequenceFile, bank.source)) {
      selected.push_back(bank);
    }
  }
  if (selected.empty() && banks.size() == 1) {
    selected.push_back(banks.front());
  }
  return selected;
}

void applySampleBinding(BankPreparationContext& context, const SonyPs1BankLayout& layout, const SamplePoolAsset& pool,
                        u32 firstSample) {
  const auto& sizes = layout.sampleSizes;
  for (auto& instrument : context.bank.instruments) {
    for (auto& region : instrument.regions) {
      if (!region.sample.needsBinding()) {
        continue;
      }
      const u32 index = region.sample.index();
      if (index >= sizes.size() || sizes[index] == 0) {
        context.fail("Sony PS1 sound bank refers outside its external sample pool", region.range);
      }
      const u32 sample = firstSample + index - static_cast<u32>(std::count(sizes.begin(), sizes.begin() + index, 0));
      if (sample >= pool.pool.samples.size()) {
        context.fail("Sony PS1 sound bank refers outside its external sample pool", region.range);
      }
      region.sample = SampleRef::resolved(pool.metadata.id, sample);
    }
  }
}

}  // namespace

DependencySelection selectSonyPs1Banks(const DependencyContext& context) {
  const auto* sequence = context.catalog().asset<SequenceProgramAsset>(context.metadata().id);
  return selectAll(chooseInstruments(*sequence, context.source(),
                                     context.catalog().assets<SequenceProgramAsset>(kSonyPs1FormatName),
                                     context.candidates<SoundBankAsset, SonyPs1BankLayout>()));
}

DependencySelection selectSonyPs1Samples(const DependencyContext& context) {
  const auto& bank = context.data<SonyPs1BankLayout>();
  const auto bodies = context.candidates<SamplePoolAsset, SonyPs1SampleBodyLayout>();
  if (!context.manual()) {
    return selectOne(bestMatches(bodies, [&](const SampleEntry& body) {
      if (body.data->length != bank.expectedSampleBytes) {
        return -1;
      }
      const bool local = sameDirectory(context.source(), body.source);
      return (local && sameStem(context.source(), body.source) ? 4 : 0) +
             (context.metadata().range.source.valid() && context.metadata().range.source == body.sourceId() ? 2 : 0) +
             (local ? 1 : 0);
    }));
  }

  std::vector<DependencyTarget> positions;
  for (const auto& body : bodies) {
    for (u32 start : findSonyPs1SampleStarts(*body.data, bank.sampleSizes)) {
      positions.push_back({body.id(), AssetPrivateData::make(SamplePosition{start})});
    }
  }
  return selectOne(positions, "Sony PS1 sound bank matches multiple external sample pools or positions");
}

void prepareSonyPs1Bank(BankPreparationContext& context, const SonyPs1BankLayout& layout) {
  for (auto& instrument : context.bank.instruments) {
    const u32 program = instrument.explicitAddress ? instrument.explicitAddress->program
                        : instrument.identity      ? instrument.identity->key & 0xff
                                                   : 0;
    instrument.explicitAddress = InstrumentAddress{.bank = context.bankIndex, .program = program};
    instrument.identity = sonyPs1InstrumentIdentity(static_cast<u16>(context.bankIndex), static_cast<u8>(program));
  }
  if (!needsExternalSamples(context.bank)) {
    return;
  }
  const auto sample =
      context.sample<SonyPs1SampleBodyLayout>("Sony PS1 sound bank has no unambiguous external sample pool");
  const auto* position = sample.placement.get<SamplePosition>();
  applySampleBinding(context, layout, sample.asset, position != nullptr ? position->firstSample : 0);
}

}  // namespace vgmtrans::formats::sony_ps1

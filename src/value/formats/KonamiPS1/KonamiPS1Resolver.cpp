/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiPS1/KonamiPS1.h"

#include "value/formats/SonyPS1/SonyPS1.h"

#include <algorithm>
#include <string>
#include <vector>

namespace vgmtrans::formats::konami_ps1 {

using namespace core;

std::vector<DesiredCollection> resolveKonamiPs1Collections(const CollectionDiscoveryContext& context) {
  const auto sequences = context.assets<SequenceProgramAsset>(kKonamiPs1FormatName);
  const auto banks = context.assets<SoundBankAsset>(sony_ps1::kSonyPs1FormatName);
  const auto samples = context.assets<SamplePoolAsset>(sony_ps1::kSonyPs1FormatName);
  std::vector<DesiredCollection> collections;
  collections.reserve(sequences.size());

  for (const auto* sequence : sequences) {
    const auto source = sequence->metadata.range.source;
    CollectionAssembly collection("source:" + std::to_string(source.valid() ? source.value : 0) +
                                      ":konami-kdt:" + std::to_string(sequence->metadata.range.offset),
                                  sequence->metadata.name);
    collection.sequence(sequence->metadata.id);

    std::vector<const SoundBankAsset*> selectedBanks;
    for (const auto* bank : banks) {
      if (source.valid() && bank->metadata.range.source == source) {
        selectedBanks.push_back(bank);
      }
    }
    if (selectedBanks.empty() && banks.size() == 1) {
      selectedBanks.push_back(banks.front());
    }
    std::ranges::sort(selectedBanks, {}, [](const SoundBankAsset* bank) { return bank->metadata.range.offset; });
    for (const auto* bank : selectedBanks) {
      collection.soundBank(bank->metadata.id);
    }
    collection.requireSoundBank();

    for (const auto* pool : samples) {
      if (source.valid() && pool->metadata.range.source == source) {
        collection.samplePool(pool->metadata.id);
      }
    }
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

}  // namespace vgmtrans::formats::konami_ps1

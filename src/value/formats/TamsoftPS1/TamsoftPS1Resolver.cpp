/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TamsoftPS1/TamsoftPS1.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace vgmtrans::formats::tamsoft_ps1 {

using namespace core;

namespace {

using SequenceEntry = AssetWithData<SequenceProgramAsset, SequenceData>;
using BankEntry = AssetWithData<SoundBankAsset, BankData>;

[[nodiscard]] std::string folded(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

[[nodiscard]] std::filesystem::path directory(const SourceFile* source) {
  if (source == nullptr) {
    return {};
  }
  const std::filesystem::path path = source->path.empty() ? std::filesystem::path(source->name) : source->path;
  return path.parent_path().lexically_normal();
}

[[nodiscard]] int matchScore(const SequenceEntry& sequence, const BankEntry& bank) {
  if (sequence.data->generation != bank.data->generation || directory(sequence.source) != directory(bank.source)) {
    return -1;
  }
  const std::string sequenceStem = folded(sequence.data->stem);
  const std::string bankStem = folded(bank.data->stem);
  if (sequenceStem == bankStem) {
    return 30;
  }
  if (!bankStem.empty() && sequenceStem.starts_with(bankStem)) {
    return 20;
  }
  if (bankStem == "BGM") {
    return 10;
  }
  return 1;
}

}  // namespace

std::vector<DesiredCollection> resolveCollections(const CollectionDiscoveryContext& context) {
  const auto sequences = context.assetsWithData<SequenceProgramAsset, SequenceData>();
  const auto banks = context.assetsWithData<SoundBankAsset, BankData>();
  std::vector<DesiredCollection> collections;
  collections.reserve(sequences.size());

  for (const auto& sequence : sequences) {
    std::vector<const BankEntry*> matches;
    int best = -1;
    for (const auto& bank : banks) {
      const int score = matchScore(sequence, bank);
      if (score < best) {
        continue;
      }
      if (score > best) {
        best = score;
        matches.clear();
      }
      if (score >= 0) {
        matches.push_back(&bank);
      }
    }
    if (matches.empty()) {
      const auto sameGeneration = [&](const BankEntry& bank) {
        return bank.data->generation == sequence.data->generation;
      };
      if (std::ranges::count_if(banks, sameGeneration) == 1) {
        matches.push_back(&*std::ranges::find_if(banks, sameGeneration));
      }
    }

    CollectionAssembly collection(
        "source:" + std::to_string(sequence.source == nullptr ? 0 : sequence.source->id.value) +
            ":song:" + std::to_string(sequence.data->song),
        sequence.asset->metadata.name);
    collection.sequence(sequence.id());
    if (!matches.empty()) {
      collection.soundBank(matches.front()->id());
      if (matches.size() > 1) {
        collection.ambiguous("Tamsoft TSQ matches multiple TVB banks equally well", sequence.id(),
                             sequence.asset->metadata.range);
      }
    } else {
      collection.requireSoundBank();
    }
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

}  // namespace vgmtrans::formats::tamsoft_ps1

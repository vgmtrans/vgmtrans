/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SquarePS2/SquarePS2.h"

#include "value/scan/CollectionDiscovery.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vgmtrans::formats::square_ps2 {

using namespace core;

namespace {

struct SequenceEntry {
  const SequenceProgramAsset* asset = nullptr;
  const SequenceData* data = nullptr;
  const SourceFile* source = nullptr;
};

struct BankEntry {
  const SoundBankAsset* asset = nullptr;
  const SoundBankData* data = nullptr;
  const SourceFile* source = nullptr;
};

[[nodiscard]] int affinity(const SourceFile* sequence, const SourceFile* bank) {
  if (sequence == nullptr || bank == nullptr) {
    return 0;
  }
  if (sequence->id == bank->id) {
    return 4;
  }
  if (sequence->parent && bank->parent && sequence->parent == bank->parent) {
    return 3;
  }
  if (!sequence->path.empty() && sequence->path == bank->path) {
    return 2;
  }
  if (!sequence->path.empty() && !bank->path.empty() && sequence->path.parent_path() == bank->path.parent_path()) {
    return 1;
  }
  return 0;
}

[[nodiscard]] std::vector<const BankEntry*> matchingBanks(const SequenceEntry& sequence,
                                                          const std::vector<BankEntry>& banks) {
  std::vector<const BankEntry*> selected;
  int best = -1;
  for (const auto& bank : banks) {
    if (bank.data->bankId != sequence.data->waveBankId) {
      continue;
    }
    const int score = affinity(sequence.source, bank.source);
    if (score > best) {
      best = score;
      selected.clear();
    }
    if (score == best) {
      selected.push_back(&bank);
    }
  }
  return selected;
}

}  // namespace

std::vector<DesiredCollection> resolveCollections(const CollectionDiscoveryContext& context) {
  std::vector<SequenceEntry> sequences;
  for (const auto& entry : context.assetsWithData<SequenceProgramAsset, SequenceData>()) {
    sequences.push_back(SequenceEntry{.asset = entry.asset, .data = entry.data, .source = entry.source});
  }
  std::vector<BankEntry> banks;
  for (const auto& entry : context.assetsWithData<SoundBankAsset, SoundBankData>()) {
    banks.push_back(BankEntry{.asset = entry.asset, .data = entry.data, .source = entry.source});
  }

  std::vector<DesiredCollection> collections;
  std::unordered_set<u32> paired;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(
        "source:" + std::to_string(sequence.source == nullptr ? 0 : sequence.source->id.value) +
            ":sequence:" + std::to_string(sequence.asset->metadata.range.offset),
        sequence.asset->metadata.name);
    collection.sequence(sequence.asset->metadata.id);
    const auto matches = matchingBanks(sequence, banks);
    if (matches.size() == 1) {
      collection.soundBank(matches.front()->asset->metadata.id);
      paired.insert(matches.front()->asset->metadata.id.value);
    } else if (matches.empty()) {
      collection.requireSoundBank();
    } else {
      for (const auto* bank : matches) {
        collection.soundBank(bank->asset->metadata.id);
        paired.insert(bank->asset->metadata.id.value);
      }
      collection.ambiguous("SquarePS2 BGM matches multiple WD banks with the same driver ID",
                           sequence.asset->metadata.id, sequence.asset->metadata.range);
    }
    collections.push_back(std::move(collection).finish());
  }

  for (const auto& bank : banks) {
    if (paired.contains(bank.asset->metadata.id.value)) {
      continue;
    }
    CollectionAssembly collection("source:" + std::to_string(bank.source == nullptr ? 0 : bank.source->id.value) +
                                      ":bank:" + std::to_string(bank.asset->metadata.id.value),
                                  bank.asset->metadata.name);
    collection.soundBank(bank.asset->metadata.id);
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindCollection(CollectionBindingContext& context) {
  if (context.sequence == nullptr || context.sequence->metadata.format != kSquarePs2FormatName) {
    return;
  }
  const auto* sequence = context.sequence->privateData.get<SequenceData>();
  if (sequence == nullptr) {
    context.fail("SquarePS2 sequence is missing retained bank binding data", context.sequence->metadata.range);
    return;
  }

  RuntimeConfig config{.defaultBank = sequence->waveBankId};
  u32 matches = 0;
  for (const auto& bank : context.soundBanks) {
    if (bank.metadata.format != kSquarePs2FormatName) {
      continue;
    }
    const auto* data = bank.privateData.get<SoundBankData>();
    if (data == nullptr) {
      context.fail("SquarePS2 WD bank is missing retained envelope data", bank.metadata.range);
      return;
    }
    if (data->bankId != sequence->waveBankId) {
      continue;
    }
    ++matches;
    config.envelopes.insert(config.envelopes.end(), data->envelopes.begin(), data->envelopes.end());
  }
  if (matches == 0) {
    context.warning("SquarePS2 BGM has no matching WD bank; dynamic ADSR reset will use sequence defaults",
                    context.sequence->metadata.range);
  } else if (matches > 1) {
    context.fail("SquarePS2 collection contains multiple WD banks with the requested driver ID",
                 context.sequence->metadata.range);
    return;
  }
  static_cast<void>(context.replaceSequenceRuntime(sequenceRuntime(std::move(config))));
}

}  // namespace vgmtrans::formats::square_ps2

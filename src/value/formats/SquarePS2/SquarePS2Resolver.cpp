/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SquarePS2/SquarePS2.h"

#include "value/scan/CollectionDiscovery.h"

#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::square_ps2 {

using namespace core;

namespace {

using SequenceEntry = AssetWithData<SequenceProgramAsset, SequenceData>;
using BankEntry = AssetWithData<SoundBankAsset, SoundBankData>;

[[nodiscard]] int sourceAffinity(const SourceFile* sequence, const SourceFile* bank) {
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
    const int score = sourceAffinity(sequence.source, bank.source);
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
  const auto sequences = context.assetsWithData<SequenceProgramAsset, SequenceData>();
  const auto banks = context.assetsWithData<SoundBankAsset, SoundBankData>();

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    const auto matches = matchingBanks(sequence, banks);
    if (matches.empty()) {
      continue;
    }
    CollectionAssembly collection(
        "source:" + std::to_string(sequence.source == nullptr ? 0 : sequence.source->id.value) +
            ":sequence:" + std::to_string(sequence.asset->metadata.range.offset),
        sequence.asset->metadata.name);
    collection.sequence(sequence.id());
    for (const auto* bank : matches) {
      collection.soundBank(bank->id());
    }
    if (matches.size() > 1) {
      collection.ambiguous("SquarePS2 BGM matches multiple WD banks with the same driver ID", sequence.id(),
                           sequence.asset->metadata.range);
    }
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
  const SoundBankData* selected = nullptr;
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
    if (selected != nullptr) {
      context.fail("SquarePS2 collection contains multiple WD banks with the requested driver ID",
                   context.sequence->metadata.range);
      return;
    }
    selected = data;
  }
  if (selected == nullptr) {
    context.warning("SquarePS2 BGM has no matching WD bank; dynamic ADSR reset will use sequence defaults",
                    context.sequence->metadata.range);
  } else {
    config.envelopes = selected->envelopes;
  }
  static_cast<void>(context.replaceSequenceRuntime(sequenceRuntime(std::move(config))));
}

}  // namespace vgmtrans::formats::square_ps2

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SquarePS2/SquarePS2.h"

#include "value/scan/AssetResolution.h"

#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::square_ps2 {

using namespace core;

namespace {

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

}  // namespace

DependencySelection WdBankId::operator()(const DependencyContext& context) const {
  const auto banks = context.candidates<SoundBankAsset, SoundBankData>();
  const auto matches = bestMatches(banks, [&](const BankEntry& bank) {
    return bank.data->bankId == value ? sourceAffinity(context.source(), bank.source) : -1;
  });
  auto selected = selectAll(matches);
  if (matches.size() > 1) {
    selected.issues.push_back(ambiguousMatchIssue("SquarePS2 BGM matches multiple WD banks with the same driver ID"));
  }
  return selected;
}

void prepareSequence(SequencePreparationContext& context, const SequenceData& sequence) {
  RuntimeConfig config{.defaultBank = sequence.waveBankId};
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
    if (data->bankId != sequence.waveBankId) {
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

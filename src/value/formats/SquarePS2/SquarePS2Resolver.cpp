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
  const auto matches = bestMatches(context.candidates<SoundBankAsset, SoundBankData>(), [&](const BankEntry& bank) {
    return bank.data->bankId == value ? sourceAffinity(context.source(), bank.source) : -1;
  });
  auto selected = selectAll(matches);
  if (matches.size() > 1) {
    selected.ambiguous(dependencyTargets(matches), "SquarePS2 BGM matches multiple WD banks with the same driver ID");
  }
  return selected;
}

SequenceRuntime prepareSequence(SequencePreparationContext& context, const SequenceData& sequence) {
  RuntimeConfig config{.defaultBank = sequence.waveBankId};
  const SoundBankData* selected = nullptr;
  for (const auto& bank : context.banks<SoundBankData>(kSquarePs2FormatName)) {
    if (bank.data.bankId != sequence.waveBankId) {
      continue;
    }
    if (selected != nullptr) {
      context.fail("SquarePS2 collection contains multiple WD banks with the requested driver ID");
    }
    selected = &bank.data;
  }
  if (selected == nullptr) {
    context.warning("SquarePS2 BGM has no matching WD bank; dynamic ADSR reset will use sequence defaults");
  } else {
    config.envelopes = selected->envelopes;
  }
  return sequenceRuntime(std::move(config));
}

}  // namespace vgmtrans::formats::square_ps2

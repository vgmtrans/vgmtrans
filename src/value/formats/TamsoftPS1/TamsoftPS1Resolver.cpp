/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TamsoftPS1/TamsoftPS1.h"
#include "value/scan/AssetResolution.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace vgmtrans::formats::tamsoft_ps1 {

using namespace core;

namespace {

using BankEntry = AssetWithData<SoundBankAsset, BankData>;

[[nodiscard]] std::string uppercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

[[nodiscard]] std::filesystem::path sourceDirectory(const SourceFile* source) {
  if (source == nullptr) {
    return {};
  }
  const std::filesystem::path path = source->path.empty() ? std::filesystem::path(source->name) : source->path;
  return path.parent_path().lexically_normal();
}

[[nodiscard]] int matchScore(const BankRequest& sequence, const SourceFile* source, const BankEntry& bank) {
  if (sequence.generation != bank.data->generation) {
    return -1;
  }
  const std::string sequenceStem = uppercase(sequence.stem);
  const std::string bankStem = uppercase(bank.data->stem);

  if (sequence.generation == Generation::Ps1) {
    // Wonderful uses the shared SYS/BGM.TVB across directories for music TSQs.
    if (sequence.usesMusicBank) {
      return bankStem == "BGM" ? 30 : -1;
    }
    return sequenceStem == bankStem && sourceDirectory(source) == sourceDirectory(bank.source) ? 30 : -1;
  }

  if (sourceDirectory(source) != sourceDirectory(bank.source)) {
    return -1;
  }
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

DependencySelection BankRequest::operator()(const DependencyContext& context) const {
  const auto banks = context.candidates<SoundBankAsset, BankData>();
  auto matches = bestMatches(banks, [&](const BankEntry& bank) { return matchScore(*this, context.source(), bank); });
  if (matches.empty() && generation == Generation::Ps2) {
    const auto sameGeneration = [&](const BankEntry& bank) { return bank.data->generation == generation; };
    if (std::ranges::count_if(banks, sameGeneration) == 1) {
      matches.push_back(&*std::ranges::find_if(banks, sameGeneration));
    }
  }
  DependencySelection result;
  if (!matches.empty()) {
    result.add(matches.front()->id());
  }
  if (matches.size() > 1) {
    for (const auto* match : matches) {
      result.alternatives.push_back(match->id());
    }
    result.issues.push_back(ambiguousMatchIssue("Tamsoft TSQ matches multiple TVB banks equally well"));
  }
  return result;
}

}  // namespace vgmtrans::formats::tamsoft_ps1

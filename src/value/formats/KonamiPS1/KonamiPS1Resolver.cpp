/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiPS1/KonamiPS1.h"

#include "value/formats/SonyPS1/SonyPS1.h"
#include "value/scan/AssetResolution.h"

#include <algorithm>
#include <string>
#include <vector>

namespace vgmtrans::formats::konami_ps1 {

using namespace core;

DependencySelection selectKonamiPs1Banks(const DependencyContext& context) {
  auto selected = context.candidates<SoundBankAsset>(sony_ps1::kSonyPs1FormatName);
  const auto source = context.metadata().range.source;
  // Keep a lone bank as a fallback; otherwise require the sequence's source.
  if (selected.size() != 1) {
    std::erase_if(selected, [&](const SoundBankAsset* bank) {
      return !source.valid() || bank->metadata.range.source != source;
    });
  }
  std::ranges::sort(selected, {}, [](const SoundBankAsset* bank) { return bank->metadata.range.offset; });
  return selectAll(selected);
}

}  // namespace vgmtrans::formats::konami_ps1

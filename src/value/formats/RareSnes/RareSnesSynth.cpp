/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/RareSnes/RareSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vgmtrans::formats::rare_snes {

using namespace core;

namespace {

[[nodiscard]] double tuningSemitones(s8 tuning) {
  return 12.0 * std::log2((1024.0 + tuning) / 1024.0);
}

[[nodiscard]] std::vector<u8> referencedSrcns(const SequenceRecipes& recipes) {
  std::vector<u8> result;
  result.reserve(recipes.patches.size());
  for (const PatchRecipe& patch : recipes.patches) {
    result.push_back(patch.srcn);
  }
  std::ranges::sort(result);
  result.erase(std::ranges::unique(result).begin(), result.end());
  return result;
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const SequenceRecipes& recipes, std::string_view displayName) {
  if (!layout.spcDirAddress || recipes.patches.empty()) {
    return std::nullopt;
  }

  const ByteReader reader = builder.reader();
  const std::vector<u8> srcns = referencedSrcns(recipes);
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, *layout.spcDirAddress, srcns);
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instrumentDraft = builder.soundBank(fmt::format("{} Instruments", displayName));
  const SnesBrrSampleRefs samples = addSnesBrrSamples(instrumentDraft.samples(), reader, catalog);

  for (const PatchRecipe& patch : recipes.patches) {
    const auto sample = samples.findSrcn(patch.srcn);
    if (!sample) {
      continue;
    }
    Instrument instrument{
        .identity =
            InstrumentIdentity{
                .domain = std::string(kInstrumentDomain),
                .key = patch.key,
            },
        .name = fmt::format("Patch {} (Program {}, SRCN {})", patch.key, patch.sourceProgram, patch.srcn),
        .range = patch.source,
    };
    auto entry = instrumentDraft.builder().append(std::move(instrument));
    if (patch.source.valid()) {
      entry.source(fmt::format("Patch {}", patch.key), patch.source, "rare-snes-instrument");
    }

    Region region{
        .sample = *sample,
        .range = patch.source,
        // Rare's pitch table reaches $1000 at source note 72. Fine tuning
        // multiplies that DSP pitch by (1024+tuning)/1024.
        .unityKey = 72.0 - tuningSemitones(patch.tuning),
        .envelope = snesDspEnvelope(patch.adsr1, patch.adsr2, patch.gain),
    };
    auto regionEntry = entry.region(*sample, std::move(region));
    if (patch.source.valid()) {
      regionEntry.source(fmt::format("Patch {} Region", patch.key), patch.source, "rare-snes-region");
    }
  }

  if (instrumentDraft.builder().empty()) {
    return std::nullopt;
  }
  return instrumentDraft;
}

}  // namespace vgmtrans::formats::rare_snes

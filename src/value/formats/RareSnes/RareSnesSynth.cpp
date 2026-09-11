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

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const SequenceRecipes& recipes, std::string_view displayName) {
  if (!layout.spcDirAddress) {
    return std::nullopt;
  }

  const ByteReader reader = builder.reader();
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, *layout.spcDirAddress, recipes.patches, &PatchRecipe::srcn);
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(bank.localSamples(), reader, catalog);

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
    auto entry = instruments.append(std::move(instrument));
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

  if (instruments.empty()) {
    return std::nullopt;
  }
  return bank;
}

}  // namespace vgmtrans::formats::rare_snes

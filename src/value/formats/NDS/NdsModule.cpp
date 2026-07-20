/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/Nds.h"

#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

struct BankAssets {
  std::optional<ScanInstrumentSetRef> instruments;
  std::array<std::optional<ScanSampleCollectionRef>, 4> samples;
};

[[nodiscard]] CollectionKey ndsCollectionKey(SourceId source, u64 sdatOffset, u32 sequenceIndex) {
  return CollectionKey{
      .resolver = std::string(kNdsFormatName),
      .value = "source:" + std::to_string(source.value) + ":sdat:" + std::to_string(sdatOffset) +
               ":seq:" + std::to_string(sequenceIndex),
  };
}

void scanNdsLayout(const NdsLayout& layout, ScanResultBuilder& result) {
  const ByteReader reader = result.reader();
  const auto psg = addNdsPsgSamples(result);

  // INFO may describe unused banks. Build only the dependency graph rooted at
  // sequences that can actually become assets.
  std::set<u16> referencedBanks;
  for (const auto& sequence : layout.sequences) {
    if (sequence.file && sequence.bank) {
      referencedBanks.insert(*sequence.bank);
    }
  }

  std::set<u16> referencedWaves;
  for (const u16 bankIndex : referencedBanks) {
    for (const auto wave : layout.banks[bankIndex].waveArchives) {
      if (wave) {
        referencedWaves.insert(*wave);
      }
    }
  }

  std::vector<std::optional<ScanSampleCollectionRef>> waveAssets(layout.waveArchives.size());
  for (const u16 waveIndex : referencedWaves) {
    const auto& wave = layout.waveArchives[waveIndex];
    if (wave.file) {
      waveAssets[waveIndex] = addNdsWaveArchive(result, *wave.file, wave.name);
    }
  }

  std::vector<BankAssets> bankAssets(layout.banks.size());
  for (const u16 bankIndex : referencedBanks) {
    const auto& bank = layout.banks[bankIndex];
    auto& assets = bankAssets[bankIndex];
    for (u32 slot = 0; slot < bank.waveArchives.size(); ++slot) {
      if (bank.waveArchives[slot]) {
        assets.samples[slot] = waveAssets[*bank.waveArchives[slot]];
      }
    }
    if (bank.file) {
      assets.instruments = addNdsInstrumentSet(result, *bank.file, bank.name, psg, assets.samples);
    }
  }

  for (u32 sequenceIndex = 0; sequenceIndex < layout.sequences.size(); ++sequenceIndex) {
    const auto& sequence = layout.sequences[sequenceIndex];
    if (!sequence.file) {
      continue;
    }

    const NdsSequenceRange range = ndsSequenceRangeForFatEntry(reader, *sequence.file);
    const SourceRange sourceRange = reader.range(range.offset, range.sequenceEnd - range.offset);
    const auto sequenceAsset = result.reserveSequence();
    result.sequence(sequenceAsset, sequence.name, sourceRange)
        .program(parseNdsSequenceProgram(reader, sequenceAsset.id, range, &result.sourceMap(), &result.diagnostics()));

    auto collection =
        result.collection(sequence.name, ndsCollectionKey(result.source(), layout.range.offset, sequenceIndex));
    collection.sequence(sequenceAsset).samples(psg);
    if (!sequence.bank) {
      continue;
    }

    const auto& assets = bankAssets[*sequence.bank];
    if (assets.instruments) {
      collection.instrumentSet(*assets.instruments);
    }
    for (const auto sample : assets.samples) {
      if (sample) {
        collection.samples(*sample);
      }
    }
  }
}

[[nodiscard]] ScanResult scanNds(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kNdsFormatName));
  for (const u32 offset : findNdsSdatOffsets(input.reader)) {
    if (const auto layout = parseNdsLayout(result, offset)) {
      scanNdsLayout(*layout, result);
    }
  }
  return result.finish();
}

}  // namespace

FormatDefinition ndsDefinition() {
  return FormatDefinition{
      .module = {.name = std::string(kNdsFormatName), .scan = scanNds},
      .sequenceDialect = ndsSequenceDialect(),
  };
}

}  // namespace vgmtrans::formats::nds

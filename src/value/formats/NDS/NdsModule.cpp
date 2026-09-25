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

// Builds the assets described by one SDAT. Dependencies are created first so
// each sequence can reference its bank directly.
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

  std::vector<std::optional<ScanSamplePoolDraft>> waveAssets(layout.waveArchives.size());
  for (const u16 waveIndex : referencedWaves) {
    const auto& wave = layout.waveArchives[waveIndex];
    if (wave.file) {
      waveAssets[waveIndex] = addNdsWaveArchive(result, *wave.file, wave.name);
    }
  }

  std::vector<std::optional<ScanSoundBankDraft>> bankAssets(layout.banks.size());
  for (const u16 bankIndex : referencedBanks) {
    const auto& bank = layout.banks[bankIndex];
    std::array<std::optional<ScanSamplePoolDraft>, 4> samples;
    for (u32 slot = 0; slot < bank.waveArchives.size(); ++slot) {
      if (bank.waveArchives[slot]) {
        samples[slot] = waveAssets[*bank.waveArchives[slot]];
      }
    }
    if (bank.file) {
      auto instruments = addNdsInstrumentSet(result, *bank.file, bank.name, psg, samples);
      if (instruments) {
        instruments->useSamples(psg);
        for (const auto& sample : samples) {
          if (sample) {
            instruments->useSamples(*sample);
          }
        }
      }
      bankAssets[bankIndex] = instruments;
    }
  }

  for (u32 sequenceIndex = 0; sequenceIndex < layout.sequences.size(); ++sequenceIndex) {
    const auto& sequence = layout.sequences[sequenceIndex];
    if (!sequence.file) {
      continue;
    }

    const NdsSequenceRange range = ndsSequenceRangeForFatEntry(reader, *sequence.file);
    const SourceRange sourceRange = reader.range(range.offset, range.sequenceEnd - range.offset);
    auto sequenceAsset = result.sequence(sequence.name, sourceRange);
    sequenceAsset.program(
        parseNdsSequenceProgram(reader, sequenceAsset.id(), range, &result.sourceMap(), &result.diagnostics()));

    sequenceAsset.collection();
    if (sequence.bank && bankAssets[*sequence.bank]) {
      sequenceAsset.useBank(*bankAssets[*sequence.bank]);
    }
  }
}

// Finds and scans every SDAT container embedded in the input source.
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

FormatModule ndsModule() {
  return FormatModule{
      .name = std::string(kNdsFormatName),
      .acceptedFormats = {source_formats::kNintendoDsRom},
      .scan = scanNds,
  };
}

}  // namespace vgmtrans::formats::nds

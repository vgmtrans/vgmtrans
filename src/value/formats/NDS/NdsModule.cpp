/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsModule.h"

#include "value/formats/NDS/NdsLayout.h"
#include "value/formats/NDS/NdsSequence.h"
#include "value/formats/NDS/NdsSynth.h"
#include "value/scan/FormatRegistry.h"
#include "value/scan/ScanResultBuilder.h"

#include <array>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

[[nodiscard]] CollectionKey ndsCollectionKey(SourceId source, u32 sdatOffset, u32 sequenceIndex) {
  return CollectionKey{
      .resolver = std::string(kNdsFormatName),
      .value = "source:" + std::to_string(source.value) + ":sdat:" + std::to_string(sdatOffset) +
               ":seq:" + std::to_string(sequenceIndex),
  };
}

[[nodiscard]] u32 boundedSectionSize(ByteReader reader, u32 offset, u32 fallbackSize) {
  if (reader.has(offset + 8, 4)) {
    const u32 storedSize = reader.le32(offset + 8) + 8;
    if (reader.has(offset, storedSize)) {
      return storedSize;
    }
  }
  return reader.has(offset, fallbackSize) ? fallbackSize : 0;
}

void annotateNdsLayout(ByteReader reader, const NdsLayout& layout, SourceMapBuilder& sourceMap) {
  sourceMap.header("SDAT Header", reader.range(layout.baseOffset, 0x24))
      .field("file_size", reader.range(layout.baseOffset + 8, 4), layout.length, SourceValueDisplay::Hex)
      .field("symb_offset", reader.range(layout.baseOffset + 0x10, 4), layout.symbOffset,
             SourceValueDisplay::Address)
      .field("info_offset", reader.range(layout.baseOffset + 0x18, 4), layout.infoOffset,
             SourceValueDisplay::Address)
      .field("fat_offset", reader.range(layout.baseOffset + 0x20, 4), layout.fatOffset, SourceValueDisplay::Address);

  if (layout.hasSymb) {
    const u32 symbSize = boundedSectionSize(reader, layout.symbOffset, 0x18);
    if (symbSize != 0) {
      sourceMap.section("SYMB Section", reader.range(layout.symbOffset, symbSize)).kind("sdat-symb");
    }
  }
  if (const u32 infoSize = boundedSectionSize(reader, layout.infoOffset, 0x18); infoSize != 0) {
    sourceMap.section("INFO Section", reader.range(layout.infoOffset, infoSize)).kind("sdat-info");
  }
  if (const u32 fatSize = boundedSectionSize(reader, layout.fatOffset, 0x0c); fatSize != 0) {
    sourceMap.table("FAT File Table", reader.range(layout.fatOffset, fatSize)).kind("sdat-fat");
  }
}

void scanNdsLayout(const ScanInput& input, const NdsLayout& layout, ScanResultBuilder& result) {
  // Build dependencies before dependents: PSG samples are universal, SWAR collections feed
  // banks, banks feed sequences, and sequences finally become exportable collections.
  const auto psg = result.sampleCollection([&](AssetId id) { return parseNdsPsgSamples(input, id); });

  std::vector<std::optional<ScanSampleCollectionRef>> waveAssetIds(layout.waveArchives.size());
  std::set<u16> referencedWaveArchives;
  for (const auto& bank : layout.banks) {
    for (const u16 waveArchive : bank.waveArchives) {
      if (waveArchive != 0xffff && waveArchive < layout.waveArchives.size()) {
        referencedWaveArchives.insert(waveArchive);
      }
    }
  }

  for (const u16 waveArchiveIndex : referencedWaveArchives) {
    const auto& waveArchive = layout.waveArchives[waveArchiveIndex];
    if (!waveArchive.valid) {
      continue;
    }
    const auto range = ndsFileRange(input.reader, layout, waveArchive.fileId);
    if (!range) {
      result.warning("NDS wave archive FAT entry was invalid", input.reader.range(layout.baseOffset, layout.length));
      continue;
    }
    if (!isNdsWaveArchive(input.reader, range->offset)) {
      continue;
    }
    waveAssetIds[waveArchiveIndex] = result.sampleCollection([&](AssetId id) {
      return parseNdsWaveArchive(input, id, *range, layout.waveArchiveNames[waveArchiveIndex], &result);
    });
  }

  std::map<u16, ScanInstrumentSetRef> bankAssetIds;
  std::set<u16> referencedBanks;
  for (const auto& sequence : layout.sequences) {
    if (sequence.valid && sequence.bank < layout.banks.size()) {
      referencedBanks.insert(sequence.bank);
    }
  }

  for (const u16 bankIndex : referencedBanks) {
    const auto& bank = layout.banks[bankIndex];
    if (!bank.valid) {
      continue;
    }
    const auto range = ndsFileRange(input.reader, layout, bank.fileId);
    if (!range) {
      result.warning("NDS instrument bank FAT entry was invalid", input.reader.range(layout.baseOffset, layout.length));
      continue;
    }

    std::array<std::optional<ScanSampleCollectionRef>, 4> waveCollections{};
    for (u32 i = 0; i < bank.waveArchives.size(); ++i) {
      const u16 waveArchive = bank.waveArchives[i];
      if (waveArchive != 0xffff && waveArchive < waveAssetIds.size()) {
        if (waveAssetIds[waveArchive]) {
          waveCollections[i] = *waveAssetIds[waveArchive];
        }
      }
    }

    auto instrumentSet = result.instrumentSet([&](AssetId id) {
      return parseNdsInstrumentSet(input, id, *range, layout.bankNames[bankIndex], result, psg, waveCollections);
    });
    bankAssetIds.emplace(bankIndex, instrumentSet);
  }

  for (u32 sequenceIndex = 0; sequenceIndex < layout.sequences.size(); ++sequenceIndex) {
    const auto& sequence = layout.sequences[sequenceIndex];
    if (!sequence.valid) {
      continue;
    }
    const auto sequenceRange = ndsFileRange(input.reader, layout, sequence.fileId);
    if (!sequenceRange) {
      result.warning("NDS sequence FAT entry was invalid", input.reader.range(layout.baseOffset, layout.length));
      continue;
    }

    const auto bankAsset = bankAssetIds.find(sequence.bank);
    const std::optional<ScanInstrumentSetRef> instrumentSet =
        bankAsset == bankAssetIds.end() ? std::nullopt : std::optional<ScanInstrumentSetRef>{bankAsset->second};
    const std::string& name = layout.sequenceNames[sequenceIndex];
    const auto sequenceAsset = result.sequence([&](AssetId id) {
      std::vector<Diagnostic> sequenceDiagnostics;
      auto asset = parseNdsSequenceProgram(
          input, id, ndsSequenceRangeForFatEntry(input.reader, sequenceRange->offset, sequenceRange->size), name,
          instrumentSet, &result.sourceMap(), &sequenceDiagnostics);
      for (auto& diagnostic : sequenceDiagnostics) {
        result.diagnostic(std::move(diagnostic));
      }
      return asset;
    });

    auto collection = result.collection(name, ndsCollectionKey(input.source.id, layout.baseOffset, sequenceIndex));
    collection.sequence(sequenceAsset).samples(psg);
    if (bankAsset != bankAssetIds.end()) {
      collection.instrumentSet(bankAsset->second);
    }
    if (sequence.bank < layout.banks.size()) {
      for (const u16 waveArchive : layout.banks[sequence.bank].waveArchives) {
        if (waveArchive != 0xffff && waveArchive < waveAssetIds.size() && waveAssetIds[waveArchive]) {
          collection.samples(*waveAssetIds[waveArchive]);
        }
      }
    }
  }
}

[[nodiscard]] bool canScanNds(const SourceFile&, std::span<const u8> bytes) {
  return !findNdsSdatOffsets(ByteReader(SourceId{}, bytes)).empty();
}

[[nodiscard]] ScanResult scanNds(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kNdsFormatName));
  for (const u32 offset : findNdsSdatOffsets(input.reader)) {
    auto header = result.cursor(input.reader.range(offset, 0x24));
    if (!header.range(0, 0x24, "NDS SDAT header")) {
      continue;
    }

    const auto layout = parseNdsLayout(input.reader, offset);
    if (!layout) {
      result.warning("NDS SDAT header was invalid", input.reader.range(offset, 0x24));
      continue;
    }
    annotateNdsLayout(input.reader, *layout, result.sourceMap());
    scanNdsLayout(input, *layout, result);
  }
  return result.finish();
}

}  // namespace

void registerNdsModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = std::string(kNdsFormatName),
      .canScan = canScanNds,
      .scan = scanNds,
  });
}

}  // namespace vgmtrans::formats::nds

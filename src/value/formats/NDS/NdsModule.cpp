/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsModule.h"

#include "value/formats/NDS/NdsLayout.h"
#include "value/formats/NDS/NdsSequence.h"
#include "value/formats/NDS/NdsSynth.h"
#include "value/scan/CollectionResolver.h"
#include "value/scan/FormatRegistry.h"

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

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

[[nodiscard]] CollectionKey ndsCollectionKey(SourceId source, u32 sdatOffset, u32 sequenceIndex) {
  return CollectionKey{
      .resolver = std::string(kNdsFormatName),
      .value = "source:" + std::to_string(source.value) + ":sdat:" + std::to_string(sdatOffset) +
               ":seq:" + std::to_string(sequenceIndex),
  };
}

void addNdsCollectionMember(ScanResult& result, const ScanInput& input, CollectionKey key, AssetId asset,
                            std::string name, CollectionMemberRole role) {
  result.matchFacts.push_back(MatchFact{
      .asset = asset,
      .format = std::string(kNdsFormatName),
      .scope = MatchScope{.kind = MatchScopeKind::Source, .source = input.source.id},
      .payload =
          CollectionMemberFact{
              .key = std::move(key),
              .collectionName = std::move(name),
              .role = role,
          },
  });
}

void scanNdsLayout(const ScanInput& input, const NdsLayout& layout, ScanResult& result) {
  // Build dependencies before dependents: PSG samples are universal, SWAR collections feed
  // banks, banks feed sequences, and sequences finally become exportable collections.
  const auto psgId = input.ids.nextAssetId();
  result.assets.emplace_back(parseNdsPsgSamples(input, psgId));

  std::vector<std::optional<AssetId>> waveAssetIds(layout.waveArchives.size());
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
      result.diagnostics.push_back(
          warning("NDS wave archive FAT entry was invalid", input.reader.range(layout.baseOffset, layout.length)));
      continue;
    }
    if (!isNdsWaveArchive(input.reader, range->offset)) {
      continue;
    }
    const auto id = input.ids.nextAssetId();
    waveAssetIds[waveArchiveIndex] = id;
    result.assets.emplace_back(parseNdsWaveArchive(input, id, *range, layout.waveArchiveNames[waveArchiveIndex]));
  }

  std::map<u16, AssetId> bankAssetIds;
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
      result.diagnostics.push_back(
          warning("NDS instrument bank FAT entry was invalid", input.reader.range(layout.baseOffset, layout.length)));
      continue;
    }

    std::array<std::optional<AssetId>, 4> waveCollections{};
    for (u32 i = 0; i < bank.waveArchives.size(); ++i) {
      const u16 waveArchive = bank.waveArchives[i];
      if (waveArchive != 0xffff && waveArchive < waveAssetIds.size()) {
        waveCollections[i] = waveAssetIds[waveArchive];
      }
    }

    const auto id = input.ids.nextAssetId();
    bankAssetIds.emplace(bankIndex, id);
    result.assets.emplace_back(
        parseNdsInstrumentSet(input, id, *range, layout.bankNames[bankIndex], psgId, waveCollections));
  }

  for (u32 sequenceIndex = 0; sequenceIndex < layout.sequences.size(); ++sequenceIndex) {
    const auto& sequence = layout.sequences[sequenceIndex];
    if (!sequence.valid) {
      continue;
    }
    const auto sequenceRange = ndsFileRange(input.reader, layout, sequence.fileId);
    if (!sequenceRange) {
      result.diagnostics.push_back(
          warning("NDS sequence FAT entry was invalid", input.reader.range(layout.baseOffset, layout.length)));
      continue;
    }

    const auto bankAsset = bankAssetIds.find(sequence.bank);
    const std::optional<AssetId> instrumentSet =
        bankAsset == bankAssetIds.end() ? std::nullopt : std::optional<AssetId>{bankAsset->second};
    const auto sequenceId = input.ids.nextAssetId();
    const std::string& name = layout.sequenceNames[sequenceIndex];
    result.assets.emplace_back(parseNdsSequenceProgram(
        input, sequenceId, ndsSequenceRangeForFatEntry(input.reader, sequenceRange->offset, sequenceRange->size), name,
        instrumentSet));

    const auto key = ndsCollectionKey(input.source.id, layout.baseOffset, sequenceIndex);
    addNdsCollectionMember(result, input, key, sequenceId, name, CollectionMemberRole::Sequence);
    addNdsCollectionMember(result, input, key, psgId, name, CollectionMemberRole::SampleCollection);
    if (instrumentSet) {
      addNdsCollectionMember(result, input, key, *instrumentSet, name, CollectionMemberRole::InstrumentSet);
    }
    if (sequence.bank < layout.banks.size()) {
      for (const u16 waveArchive : layout.banks[sequence.bank].waveArchives) {
        if (waveArchive != 0xffff && waveArchive < waveAssetIds.size() && waveAssetIds[waveArchive]) {
          addNdsCollectionMember(result, input, key, *waveAssetIds[waveArchive], name,
                                 CollectionMemberRole::SampleCollection);
        }
      }
    }
  }
}

[[nodiscard]] bool canScanNds(const SourceFile&, std::span<const u8> bytes) {
  return !findNdsSdatOffsets(ByteReader(SourceId{}, bytes)).empty();
}

[[nodiscard]] ScanResult scanNds(const ScanInput& input) {
  ScanResult result;
  for (const u32 offset : findNdsSdatOffsets(input.reader)) {
    const auto layout = parseNdsLayout(input.reader, offset);
    if (!layout) {
      result.diagnostics.push_back(warning("NDS SDAT header was invalid", input.reader.range(offset, 0x24)));
      continue;
    }
    scanNdsLayout(input, *layout, result);
  }
  return result;
}

[[nodiscard]] std::vector<DesiredCollection> resolveNdsCollections(const MatchContext& context) {
  return resolveCollectionMemberFacts(context, kNdsFormatName, kNdsFormatName);
}

}  // namespace

void registerNdsModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = std::string(kNdsFormatName),
      .canScan = canScanNds,
      .scan = scanNds,
      .resolveCollections = resolveNdsCollections,
  });
}

}  // namespace vgmtrans::formats::nds

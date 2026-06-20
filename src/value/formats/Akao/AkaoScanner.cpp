/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoScanner.h"

#include "value/formats/Akao/AkaoBytecode.h"
#include "value/formats/Akao/AkaoFacts.h"
#include "value/formats/Akao/AkaoSequence.h"
#include "value/formats/Akao/AkaoSynth.h"
#include "value/formats/Akao/AkaoVersion.h"
#include "value/scan/ScanResultBuilder.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

[[nodiscard]] bool hasFf7HardcodedSampleData(ByteReader reader) {
  return reader.size() >= 0x1a8000 && reader.has(0xe0000, 4) && reader.has(0x156000, 4) &&
         reader.le32(0xe0000) == 0x1010 && reader.le32(0x156000) == 0x1010;
}

[[nodiscard]] bool hasAkaoSignature(ByteReader reader) {
  for (u64 offset = 0; offset + 0x10 <= reader.size(); ++offset) {
    if (reader.be32(offset) == kAkaoSignature) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::vector<u32> akaoOffsets(ByteReader reader) {
  std::vector<u32> offsets;
  for (u64 offset = 0; offset + 0x10 <= reader.size(); ++offset) {
    if (reader.be32(offset) == kAkaoSignature) {
      offsets.push_back(static_cast<u32>(offset));
    }
  }
  return offsets;
}

void addSampleFacts(ScanResultBuilder& result, const AkaoSampleCollectionParse& parsed) {
  std::vector<MatchField> fields{
      MatchField{.name = std::string(kAkaoFieldFirstArt), .value = std::to_string(parsed.firstArtId)},
      MatchField{.name = std::string(kAkaoFieldArtCount), .value = std::to_string(parsed.artCount)},
      MatchField{.name = std::string(kAkaoFieldScanOrdinal), .value = std::to_string(parsed.scanOrdinal)},
  };
  addOptionalFactField(fields, std::string(kAkaoFieldSampleSetId),
                       parsed.sampleSetId ? std::optional<u32>{*parsed.sampleSetId} : std::nullopt);
  result.sourceFact(parsed.ref.id, akaoFact(std::string(kAkaoFactSampleCollection), std::move(fields)));
}

void addSequenceFacts(ScanResultBuilder& result, ScanSequenceRef sequence, const AkaoSequenceAnalysis& analysis) {
  std::vector<MatchField> fields{
      MatchField{.name = std::string(kAkaoFieldSequenceId), .value = std::to_string(analysis.header.sequenceId)},
      MatchField{.name = std::string(kAkaoFieldOffset), .value = std::to_string(analysis.header.offset)},
      MatchField{.name = std::string(kAkaoFieldVersion), .value = versionName(analysis.header.version)},
  };
  addOptionalFactField(fields, std::string(kAkaoFieldSampleSetId),
                       analysis.header.sampleSetId ? std::optional<u32>{*analysis.header.sampleSetId} : std::nullopt);
  result.sourceFact(sequence.id, akaoFact(std::string(kAkaoFactSequence), std::move(fields)));
}

void addInstrumentFacts(ScanResultBuilder& result, ScanInstrumentSetRef instrument, const AkaoSequenceAnalysis& analysis,
                        std::span<const u32> requiredArts) {
  result.sourceFact(instrument.id, akaoFact(std::string(kAkaoFactInstrumentSet), {
                                           MatchField{.name = std::string(kAkaoFieldSequenceId),
                                                      .value = std::to_string(analysis.header.sequenceId)},
                                       }));
  for (const u32 artId : requiredArts) {
    if (artId == 0) {
      continue;
    }
    result.sourceFact(instrument.id, akaoFact(std::string(kAkaoFactRequiredArticulation), {
                                             MatchField{.name = std::string(kAkaoFieldSequenceId),
                                                        .value = std::to_string(analysis.header.sequenceId)},
                                             MatchField{.name = std::string(kAkaoFieldArtId),
                                                        .value = std::to_string(artId)},
                                         }));
  }
}

std::vector<AkaoSampleCollectionParse> scanSampleCollections(const ScanInput& input, ScanResultBuilder& result,
                                                            std::span<const u32> offsets) {
  const AkaoPs1Version sourceVersion = determineVersionFromSource(input.source);
  std::vector<AkaoSampleCollectionParse> sampleCollections;
  u32 scanOrdinal = 0;

  for (const u32 offset : offsets) {
    if (input.reader.le16(offset + 6) != 0 || !isPossibleAkaoSampleCollection(input.reader, offset)) {
      continue;
    }
    AkaoPs1Version version = sourceVersion == AkaoPs1Version::Unknown ? guessSampleVersion(input.reader, offset)
                                                                       : sourceVersion;
    auto ref = result.reserveSampleCollection();
    if (auto parsed = parseAkaoSampleCollection(input, result, ref, offset, version, scanOrdinal++)) {
      addSampleFacts(result, *parsed);
      sampleCollections.push_back(std::move(*parsed));
    } else {
      result.warning("Akao sample collection header was detected but sample data could not be parsed",
                     input.reader.range(offset, 0x40));
    }
  }

  if (hasFf7HardcodedSampleData(input.reader)) {
    auto ref = result.reserveSampleCollection();
    if (auto parsed = parseAkaoSampleCollection(
            input, result, ref,
            AkaoInstrDatLocation{.instrAllOffset = 0xe0000, .instrDatOffset = 0x156000, .firstArtId = 0, .artCount = 128},
            scanOrdinal++)) {
      addSampleFacts(result, *parsed);
      sampleCollections.push_back(std::move(*parsed));
    }
  }

  return sampleCollections;
}

void scanSequences(const ScanInput& input, ScanResultBuilder& result, std::span<const u32> offsets,
                   const AkaoArtMap& artMap) {
  for (const u32 offset : offsets) {
    if (input.reader.le16(offset + 6) == 0) {
      continue;
    }
    auto analysis = analyzeAkaoSequence(input.reader, input.source, offset);
    if (!analysis) {
      continue;
    }

    const auto sequenceRef = result.reserveSequence();
    const auto instrumentRef = result.reserveInstrumentSet();
    static_cast<void>(result.sequence(sequenceRef, [&](AssetId id) {
      std::vector<Diagnostic> diagnostics;
      auto asset =
          parseAkaoSequenceProgram(input, id, *analysis, instrumentRef, &result.sourceMap(), &diagnostics);
      for (auto& diagnostic : diagnostics) {
        result.diagnostic(std::move(diagnostic));
      }
      return asset;
    }));
    static_cast<void>(result.instrumentSet(
        instrumentRef, [&](AssetId id) { return parseAkaoInstrumentSet(input, id, *analysis, artMap); }));

    const auto required = requiredArticulations(input.reader, *analysis);
    addSequenceFacts(result, sequenceRef, *analysis);
    addInstrumentFacts(result, instrumentRef, *analysis, required);
  }
}

}  // namespace

bool canScanAkao(const SourceFile& source, std::span<const u8> bytes) {
  ByteReader reader(source.id, bytes);
  if (hasFf7HardcodedSampleData(reader)) {
    return true;
  }
  return hasAkaoSignature(reader);
}

ScanResult scanAkao(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kAkaoFormatName), std::string(kAkaoCollectionResolver));
  const auto offsets = akaoOffsets(input.reader);
  const auto sampleCollections = scanSampleCollections(input, result, offsets);
  scanSequences(input, result, offsets, buildAkaoArtMap(sampleCollections));
  return result.finish();
}

}  // namespace vgmtrans::formats::akao

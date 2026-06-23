/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoScanner.h"

#include "value/formats/Akao/AkaoInstrumentSet.h"
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

[[nodiscard]] bool isAkaoSequenceCandidate(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 0x10) || reader.be32(offset) != kAkaoSignature || reader.le16(offset + 6) == 0) {
    return false;
  }
  const AkaoProfile profile = akaoProfile(guessSequenceVersion(reader, offset));
  const u32 bitsOffset = profile.trackAllocationBitsOffset();
  if (!reader.has(offset + bitsOffset, 4)) {
    return false;
  }
  const u32 trackBits = reader.le32(offset + bitsOffset);
  if (!profile.version3OrLater() && (trackBits & ~0xffffffu) != 0) {
    return false;
  }
  if (profile.version3OrLater()) {
    if (!reader.has(offset + 0x40, 1)) {
      return false;
    }
    if (reader.le32(offset + 0x28) != 0 || reader.le32(offset + 0x2c) != 0 || reader.le32(offset + 0x38) != 0 ||
        reader.le32(offset + 0x3c) != 0) {
      return false;
    }
  }
  return true;
}

void addSampleFacts(ScanResultBuilder& result, const AkaoSampleCollectionParse& parsed) {
  if (parsed.sampleSetId) {
    result.sourceFact(parsed.ref.id,
                      IdMatchFact{.domain = std::string(kAkaoSampleSetDomain), .value = *parsed.sampleSetId});
  }
  result.sourceFact(parsed.ref.id, OffsetOrderFact{.offset = parsed.offset});
  result.sourceFact(parsed.ref.id, SampleCoverageFact{
                                       .domain = std::string(kAkaoArticulationDomain),
                                       .first = parsed.firstArtId,
                                       .count = parsed.artCount,
                                   });
}

void addSequenceFacts(ScanResultBuilder& result, ScanSequenceRef sequence, const AkaoSequenceAnalysis& analysis) {
  result.sourceFact(sequence.id,
                    IdMatchFact{.domain = std::string(kAkaoSequenceIdDomain), .value = analysis.header.sequenceId});
  if (analysis.header.sampleSetId) {
    result.sourceFact(sequence.id,
                      IdMatchFact{.domain = std::string(kAkaoSampleSetDomain), .value = *analysis.header.sampleSetId});
  }
  result.sourceFact(sequence.id, OffsetOrderFact{.offset = analysis.header.offset});
}

void addInstrumentFacts(ScanResultBuilder& result, ScanInstrumentSetRef instrument,
                        const AkaoSequenceAnalysis& analysis, std::span<const u32> requiredArts) {
  result.sourceFact(instrument.id,
                    IdMatchFact{.domain = std::string(kAkaoSequenceIdDomain), .value = analysis.header.sequenceId});
  std::vector<u32> required;
  for (const u32 art : requiredArts) {
    if (art != 0) {
      required.push_back(art);
    }
  }
  if (!required.empty()) {
    result.sourceFact(instrument.id, SampleRequirementFact{
                                         .domain = std::string(kAkaoArticulationDomain),
                                         .required = std::move(required),
                                     });
  }
}

void scanSampleCollections(const ScanInput& input, ScanResultBuilder& result, std::span<const u32> offsets) {
  const AkaoPs1Version sourceVersion = determineVersionFromSource(input.source);

  for (const u32 offset : offsets) {
    if (input.reader.le16(offset + 6) != 0 || !isPossibleAkaoSampleCollection(input.reader, offset)) {
      continue;
    }
    AkaoPs1Version version =
        sourceVersion == AkaoPs1Version::Unknown ? guessSampleVersion(input.reader, offset) : sourceVersion;
    auto ref = result.reserveSampleCollection();
    if (auto parsed = parseAkaoSampleCollection(input, result, ref, offset, version)) {
      addSampleFacts(result, *parsed);
    } else {
      result.warning("Akao sample collection header was detected but sample data could not be parsed",
                     input.reader.range(offset, 0x40));
    }
  }

  if (const auto hardcoded = ff7HardcodedAkaoSampleLocation(input.reader)) {
    auto ref = result.reserveSampleCollection();
    if (auto parsed = parseAkaoSampleCollection(input, result, ref, *hardcoded)) {
      addSampleFacts(result, *parsed);
    }
  }
}

void scanSequences(const ScanInput& input, ScanResultBuilder& result, std::span<const u32> offsets) {
  for (const u32 offset : offsets) {
    if (!isAkaoSequenceCandidate(input.reader, offset)) {
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
      auto asset = parseAkaoSequenceProgram(input, id, *analysis, instrumentRef, &result.sourceMap(), &diagnostics);
      for (auto& diagnostic : diagnostics) {
        result.diagnostic(std::move(diagnostic));
      }
      return asset;
    }));
    auto parsedInstrumentSet = parseAkaoInstrumentSet(input, instrumentRef.id, *analysis, {});
    const auto required = parsedInstrumentSet.requiredArticulations;
    static_cast<void>(result.instrumentSet(
        instrumentRef, [asset = std::move(parsedInstrumentSet.asset)](AssetId) mutable { return std::move(asset); }));

    addSequenceFacts(result, sequenceRef, *analysis);
    addInstrumentFacts(result, instrumentRef, *analysis, required);
  }
}

}  // namespace

bool canScanAkao(const SourceFile& source, std::span<const u8> bytes) {
  ByteReader reader(source.id, bytes);
  if (ff7HardcodedAkaoSampleLocation(reader)) {
    return true;
  }
  return hasAkaoSignature(reader);
}

ScanResult scanAkao(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kAkaoFormatName), std::string(kAkaoCollectionResolver));
  const auto offsets = akaoOffsets(input.reader);
  scanSampleCollections(input, result, offsets);
  scanSequences(input, result, offsets);
  return result.finish();
}

}  // namespace vgmtrans::formats::akao

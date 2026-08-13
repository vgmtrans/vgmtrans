/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"

#include <fmt/format.h>

#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

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
  if (parsed.sampleSetId) {
    result.sourceFact(parsed.ref.id,
                      IdMatchFact{.domain = std::string(kAkaoSampleSetDomain), .value = *parsed.sampleSetId});
  }
  result.sourceFact(parsed.ref.id, SampleCoverageFact{
                                       .domain = std::string(kAkaoArticulationDomain),
                                       .first = parsed.firstArticulationId,
                                       .count = parsed.articulationCount,
                                   });
}

void addSequenceFacts(ScanResultBuilder& result, ScanSequenceRef sequence, const AkaoSequenceAnalysis& analysis,
                      std::span<const u32> requiredArticulations) {
  result.sourceFact(sequence.id,
                    IdMatchFact{.domain = std::string(kAkaoSequenceIdDomain), .value = analysis.header.sequenceId});
  if (analysis.header.sampleSetId) {
    result.sourceFact(sequence.id,
                      IdMatchFact{.domain = std::string(kAkaoSampleSetDomain), .value = *analysis.header.sampleSetId});
  }
  std::vector<u32> required;
  for (const u32 articulation : requiredArticulations) {
    if (articulation != 0) {
      required.push_back(articulation);
    }
  }
  if (!required.empty()) {
    result.sourceFact(sequence.id, SampleRequirementFact{
                                       .domain = std::string(kAkaoArticulationDomain),
                                       .required = std::move(required),
                                   });
  }
}

void addInstrumentSetFacts(ScanResultBuilder& result, ScanInstrumentSetRef instruments, ScanSequenceRef sequence) {
  result.sourceFact(instruments.id, AssetRelationFact{
                                        .domain = std::string(kAkaoInstrumentSequenceDomain),
                                        .target = sequence.id,
                                    });
}

void scanSampleCollections(const ScanInput& input, ScanResultBuilder& result, std::span<const u32> offsets,
                           std::optional<AkaoSplitSampleLocation> hardcodedSampleLocation) {
  const AkaoPs1Version sourceVersion = determineVersionFromSource(input.source);

  for (const u32 offset : offsets) {
    if (input.reader.le16(offset + 6) != 0 || !isPossibleAkaoSampleCollection(input.reader, offset)) {
      continue;
    }
    const AkaoPs1Version version =
        sourceVersion == AkaoPs1Version::Unknown ? guessSampleVersion(input.reader, offset) : sourceVersion;
    if (auto parsed = parseAkaoSampleCollection(input, result, offset, version)) {
      addSampleFacts(result, *parsed);
    } else {
      result.warning("Akao sample collection header was detected but sample data could not be parsed",
                     input.reader.range(offset, 0x40));
    }
  }

  if (hardcodedSampleLocation) {
    if (auto parsed = parseAkaoSampleCollection(input, result, *hardcodedSampleLocation)) {
      addSampleFacts(result, *parsed);
    }
  }
}

void scanSequences(const ScanInput& input, ScanResultBuilder& result, std::span<const u32> offsets) {
  for (const u32 offset : offsets) {
    const auto layout = readAkaoSequenceLayout(input, offset);
    if (!layout) {
      continue;
    }

    const std::string sequenceName = fmt::format("Akao Seq {:02X}", layout->header.sequenceId);
    auto sequence = result.sequence(sequenceName, input.reader.range(offset, layout->header.length));
    auto parsed = parseAkaoSequence(input, sequence.id(), *layout, &result.sourceMap(), &result.diagnostics());
    auto instruments = result.instrumentSet(akaoInstrumentSetName(parsed.analysis));
    parsed.analysis.requiredArticulations = buildAkaoInstrumentSet(input, parsed.analysis, {}, instruments.builder());
    addSequenceFacts(result, sequence.ref(), parsed.analysis, parsed.analysis.requiredArticulations);
    addInstrumentSetFacts(result, instruments.ref(), sequence.ref());
    sequence.program(std::move(parsed.program));
  }
}

[[nodiscard]] ScanResult scanAkao(const ScanInput& input) {
  const auto offsets = akaoOffsets(input.reader);
  const auto hardcodedSampleLocation = ff7HardcodedAkaoSampleLocation(input.reader);
  if (offsets.empty() && !hardcodedSampleLocation) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kAkaoFormatName), std::string(kAkaoCollectionResolver));
  scanSampleCollections(input, result, offsets, hardcodedSampleLocation);
  scanSequences(input, result, offsets);
  return result.finish();
}

}  // namespace

FormatDefinition akaoDefinition() {
  return FormatDefinition{
      .module =
          {
              .name = std::string(kAkaoFormatName),
              .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
              .acceptedFormats = {source_formats::kPlayStationRam},
              .scan = scanAkao,
              .collectionResolverId = std::string(kAkaoCollectionResolver),
              .resolveCollections = resolveAkaoCollections,
              .prepareCollection = prepareAkaoCollection,
          },
      .sequenceDialects = akaoSequenceDialects(),
  };
}

}  // namespace vgmtrans::formats::akao

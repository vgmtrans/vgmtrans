/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/scan/FormatDefinition.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/session/Session.h"

namespace {

void formatRegistryStoresCopyableModuleValues() {
  FormatRegistry registry;
  registry.add(probeSequenceModule());
  registry.add(FormatModule{
      .name = std::string("DynamicProbe"),
      .scan = scanProbeSequence,
  });

  const FormatRegistry copy = registry;
  const std::array<u8, 1> probeBytes{0xaa};
  expect(copy.modules().size() == 2, "format registry should copy registered module values");
  expect(copy.modules()[0].name == "ProbeSequence", "format registry should preserve copied module names");
  expect(copy.modules()[1].name == "DynamicProbe", "format registry should own dynamically registered module names");
  expect(copy.modules()[0].canScan(SourceFile{}, probeBytes),
         "format registry should preserve copied module scan predicates");
  expect(copy.modules()[1].canScan == nullptr,
         "format registry should accept scan-only modules without a duplicate recognition probe");

  bool threw = false;
  try {
    registry.add(FormatModule{
        .name = "Broken",
    });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "format registry should reject incomplete module values");
}

void sequenceDialectRegistryStoresCopyableDialectValues() {
  SequenceDialectRegistry registry;
  registry.add(probeSequenceDialect());

  const SequenceDialectRegistry copy = registry;
  const auto* dialect = copy.find("probe");
  expect(dialect != nullptr, "sequence dialect registry should copy registered dialect values");
  expect(dialect->execute != nullptr, "sequence dialect registry should preserve copied command executor");
  expect(copy.find("Missing") == nullptr, "sequence dialect registry should return null for a missing dialect");
  expect(copy.contains("probe"), "sequence dialect registry should report copied dialect keys");

  bool threw = false;
  try {
    registry.add(SequenceDialect{});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "sequence dialect registry should reject dialects with empty IDs");
}

void sessionRegistersOneFormatDefinitionAtTheAuthoringSurface() {
  Session session;
  session.registerFormat(FormatDefinition{
      .module = probeSequenceModule(),
      .sequenceDialect = probeSequenceDialect(),
  });

  expect(session.formats().modules().size() == 1 && session.formats().modules()[0].name == "ProbeSequence",
         "format definition should register its scanner");
  expect(session.dialects().contains("probe"), "format definition should register its executor family");
}

void scanResultBuilderCoversCommonScannerPlumbing() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder.probe"}, {0xaa, 0xbb, 0xcc});
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto wholeSource = input.reader.range(0, input.reader.size());

  const auto sequence = out.sequence("Builder Sequence", wholeSource)
                            .program(SequenceProgram{
                                .dialect = DialectId{.value = "probe"},
                                .timebase = Timebase{.ppqn = 48},
                            });
  const auto bank = out.instrumentSet("Builder Bank", input.reader.range(0, 1)).instruments({});

  SampleCollection sampleCollection;
  sampleCollection.samples.push_back(Sample{
      .name = "Builder Sample",
      .codec = AudioCodec::PcmS8,
      .encodedData = input.reader.range(1, 2),
      .sampleRate = 32000,
      .channels = 1,
      .bitsPerSample = 8,
  });
  const auto samples =
      out.sampleCollection("Builder Samples", input.reader.range(1, 2)).samples(std::move(sampleCollection));

  out.collection("Builder Song", CollectionKey{.resolver = "ProbeBuilder", .value = "song:1"})
      .sequence(sequence)
      .instrumentSet(bank)
      .samples(samples);
  out.warning("builder warning", input.reader.range(0, 1));

  ScanResult result = out.finish();
  expect(result.assets.size() == 3, "scan result builder should add sequence, instrument, and sample assets");
  expect(metadata(result.assets[0]).id == AssetId{0} && metadata(result.assets[0]).format == "ProbeBuilder",
         "scan result builder should assign sequence metadata");
  expect(metadata(result.assets[1]).id == AssetId{1}, "scan result builder should assign instrument metadata");
  expect(metadata(result.assets[2]).id == AssetId{2}, "scan result builder should assign sample metadata");
  expect(result.matchFacts.empty(), "scan result builder should not need match facts for explicit collections");
  expect(result.explicitCollections.size() == 1, "scan result builder should emit one explicit collection");
  expect(result.explicitCollections[0].sequence == sequence.id,
         "scan result builder should preserve the collection sequence");
  expect(result.explicitCollections[0].instrumentSets == std::vector<AssetId>{bank.id},
         "scan result builder should preserve the collection instrument set");
  expect(result.explicitCollections[0].sampleCollections == std::vector<AssetId>{samples.id},
         "scan result builder should preserve the collection sample collection");
  expect(result.diagnostics.size() == 1 && result.diagnostics[0].message == "builder warning",
         "scan result builder should preserve diagnostics");
}

void scanResultBuilderNamesSourceCollections() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "fallback.spc", .title = "Tagged Song"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };

  ScanResultBuilder out(input, "ProbeBuilder");
  expect(out.sourceDisplayName() == "Tagged Song", "source display name should prefer source metadata");
  static_cast<void>(out.sourceCollection(out.sourceDisplayName()));

  const ScanResult result = out.finish();
  expect(result.explicitCollections.size() == 1, "source collection helper should create one collection");
  expect(result.explicitCollections[0].key.resolver == "ProbeBuilder" &&
             result.explicitCollections[0].key.value == "source:" + std::to_string(source.value),
         "source collection identity should not depend on its display name");
}

void scanResultBuilderRejectsReferencedUncommittedHandles() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-uncommitted.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto sequence = out.reserveSequence();
  out.collection("Broken").sequence(sequence);

  bool threw = false;
  try {
    static_cast<void>(out.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject referenced handles that were never added");
}

void scanResultBuilderRejectsWrongRoleHandleReuse() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-wrong-role.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto sequence = out.reserveSequence();

  bool threw = false;
  try {
    out.collection("Broken").instrumentSet(ScanInstrumentSetRef{.id = sequence.id});
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject using one handle id with the wrong role");
}

void scanResultBuilderRejectsUncommittedSampleRefs() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-sample-ref.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto samples = out.reserveSampleCollection();
  const SampleRef ref = out.sampleRef(samples, 7);
  expect(ref.collection == samples.id && ref.index == 7, "scan result builder should create typed sample refs");

  bool threw = false;
  try {
    static_cast<void>(out.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject sample refs to collections that were never added");
}

void scanResultBuilderCursorReportsMalformedFields() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "cursor.probe"}, {0xaa, 0xbb, 0xcc});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  auto validCursor = out.cursor(input.reader.range(1, 2));
  const auto value = validCursor.le16(0, "probe value");
  expect(value && *value == 0xccbb, "parse cursor should return parsed field values");
  expect(sameRange(value.range, SourceRange{.source = source, .offset = 1, .size = 2}),
         "parse cursor should return parsed field ranges");
  out.sourceMap().header("Probe Header", input.reader.range(1, 2)).field("probe_value", value);

  auto cursor = out.cursor(input.reader.range(2, 1));
  expect(!cursor.le32(0, "probe field"), "parse cursor should reject fields outside its range");

  const ScanResult result = out.finish();
  const auto headerIds = result.sourceMap.withRole(source, SourceRole::Header);
  expect(headerIds.size() == 1, "ranged parse values should be accepted by annotation fields");
  const auto& header = result.sourceMap.get(headerIds[0]);
  expect(header.fields.size() == 1 && header.fields[0].name == "probe_value" &&
             std::get<u64>(header.fields[0].value) == 0xccbb &&
             sameRange(header.fields[0].range, SourceRange{.source = source, .offset = 1, .size = 2}),
         "annotation fields should use the parsed value range");
  expect(result.diagnostics.size() == 1, "parse cursor should report malformed fields as diagnostics");
  expect(result.diagnostics[0].message == "Could not read probe field: field is outside the parser range",
         "parse cursor diagnostic should name the failed field");
}

}  // namespace

void runValueRegistryTests() {
  formatRegistryStoresCopyableModuleValues();
  sequenceDialectRegistryStoresCopyableDialectValues();
  sessionRegistersOneFormatDefinitionAtTheAuthoringSurface();
  scanResultBuilderCoversCommonScannerPlumbing();
  scanResultBuilderNamesSourceCollections();
  scanResultBuilderRejectsReferencedUncommittedHandles();
  scanResultBuilderRejectsWrongRoleHandleReuse();
  scanResultBuilderRejectsUncommittedSampleRefs();
  scanResultBuilderCursorReportsMalformedFields();
}

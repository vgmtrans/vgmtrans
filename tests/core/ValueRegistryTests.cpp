/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/session/Session.h"

namespace {

void formatRegistryStoresCopyableModulesAtomically() {
  FormatRegistry registry;
  registry.add(probeSequenceModule());
  registry.add(FormatModule{
      .name = std::string("DynamicProbe"),
      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
      .scan = scanProbeSequence,
  });
  registry.add(SourceExtractor{
      .name = "DynamicExtractor",
      .acceptedFormats = {"probe-container"},
      .extract = [](const ExtractionInput&) { return ExtractionResult{}; },
  });

  const FormatRegistry copy = registry;
  expect(copy.modules().size() == 2, "format registry should copy registered module values");
  expect(copy.modules()[0].name == "ProbeSequence", "format registry should preserve copied module names");
  expect(copy.modules()[1].name == "DynamicProbe", "format registry should own dynamically registered module names");
  expect(copy.modules()[0].scan && copy.modules()[1].scan, "format registry should preserve copied module scanners");
  expect(copy.modules()[0].preferredSampleFilter == SampleFilter::None,
         "formats should prefer no sample filtering unless they opt into a filter");
  expect(copy.findModule("DynamicProbe") != nullptr &&
             copy.findModule("DynamicProbe")->preferredSampleFilter == SampleFilter::SnesDspLowPass,
         "format registry should expose a format's preferred sample filter");
  expect(copy.findModule("Missing") == nullptr, "format registry should report missing modules");
  expect(copy.extractors().size() == 1 && copy.extractors().front().name == "DynamicExtractor",
         "format registry should copy source extractor values");
  bool threw = false;
  try {
    registry.add(FormatModule{.name = "Broken"});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "format registry should reject incomplete module values");

  threw = false;
  try {
    registry.add(FormatModule{
        .name = "DuplicateAcceptedFormat",
        .acceptedFormats = {"same", "same"},
        .scan = scanProbeSequence,
    });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw && registry.modules().size() == 2,
         "format registry should reject duplicate accepted formats without partially registering a module");
}

void sessionRegistersOneFormatModuleAtTheAuthoringSurface() {
  Session session;
  session.registerFormat(probeSequenceModule());

  expect(session.formats().modules().size() == 1 && session.formats().modules()[0].name == "ProbeSequence",
         "format module should register its scanner");
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

  const auto sequence = out.sequence("Builder Sequence", wholeSource).program(probeSequenceDialect().makeProgram());
  const auto bank = out.instrumentSet("Builder Bank", input.reader.range(0, 1));
  auto samples = out.sampleCollection("Builder Samples", input.reader.range(1, 2));
  samples.add(0, Sample{
                     .name = "Builder Sample",
                     .codec = AudioCodec::PcmS8,
                     .encodedData = input.reader.range(1, 2),
                     .sampleRate = 32000,
                     .channels = 1,
                     .bitsPerSample = 8,
                 });

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
  expect(result.explicitCollections[0].members.sequence == sequence.id(),
         "scan result builder should preserve the collection sequence");
  expect(result.explicitCollections[0].members.instrumentSets == std::vector<AssetId>{bank.id()},
         "scan result builder should preserve the collection instrument set");
  expect(result.explicitCollections[0].members.sampleCollections == std::vector<AssetId>{samples.id()},
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

void scanResultBuilderRejectsIncompleteSequenceDrafts() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-uncommitted.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto sequence = out.sequence("Incomplete Sequence");
  out.collection("Broken").sequence(sequence);

  bool threw = false;
  try {
    static_cast<void>(out.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject a sequence draft that was never given a program");
}

void scanResultBuilderRejectsWrongRoleHandleReuse() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-wrong-role.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto sequence = out.sequence("Sequence").program(SequenceProgram{});

  bool threw = false;
  try {
    out.collection("Broken").instrumentSet(ScanInstrumentSetRef{.id = sequence.id()});
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject using one handle id with the wrong role");
}

void scanResultBuilderPublishesEmptySynthDrafts() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-sample-ref.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto samples = out.sampleCollection("Recognized Samples");
  const auto instruments = out.instrumentSet("Recognized Instruments");
  const ScanResult result = out.finish();
  expect(result.assets.size() == 2 && metadata(result.assets[0]).id == samples.id() &&
             metadata(result.assets[1]).id == instruments.id(),
         "creating a synth draft should publish it in creation order even when it remains empty");
  expect(std::get<SampleCollectionAsset>(result.assets[0]).samples.samples.empty() &&
             std::get<InstrumentSetAsset>(result.assets[1]).instruments.empty(),
         "empty published synth assets should remain ordinary visible assets");
}

void scanResultBuilderCursorReportsMalformedFields() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "cursor.probe"}, {0xaa, 0xbb, 0xcc});
  ScanIdAllocator ids;
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};

  ScanResultBuilder out(input, "ProbeBuilder");
  RecordReader validCursor(input.reader, 1, 3, &out.diagnostics(), false);
  const auto value = validCursor.u16leAt(0, "probe value");
  expect(value && *value == 0xccbb, "parse cursor should return parsed field values");
  expect(sameRange(value.range, SourceRange{.source = source, .offset = 1, .size = 2}),
         "parse cursor should return parsed field ranges");
  out.sourceMap().header("Probe Header", input.reader.range(1, 2)).field("probe_value", value);

  RecordReader cursor(input.reader, 2, 3, &out.diagnostics(), false);
  expect(!cursor.u32leAt(0, "probe field"), "record reader should reject fields outside its range");

  const ScanResult result = out.finish();
  const auto headerIds = result.sourceMap.withRole(source, SourceRole::Header);
  expect(headerIds.size() == 1, "ranged parse values should be accepted by annotation fields");
  const auto& header = result.sourceMap.get(headerIds[0]);
  expect(header.fields.size() == 1 && header.fields[0].name == "probe_value" &&
             std::get<u64>(header.fields[0].value) == 0xccbb &&
             sameRange(header.fields[0].range, SourceRange{.source = source, .offset = 1, .size = 2}),
         "annotation fields should use the parsed value range");
  expect(result.diagnostics.size() == 1, "parse cursor should report malformed fields as diagnostics");
  expect(result.diagnostics[0].message == "Truncated field 'probe field'",
         "record reader diagnostic should name the failed field");
}

}  // namespace

void runValueRegistryTests() {
  formatRegistryStoresCopyableModulesAtomically();
  sessionRegistersOneFormatModuleAtTheAuthoringSurface();
  scanResultBuilderCoversCommonScannerPlumbing();
  scanResultBuilderNamesSourceCollections();
  scanResultBuilderRejectsIncompleteSequenceDrafts();
  scanResultBuilderRejectsWrongRoleHandleReuse();
  scanResultBuilderPublishesEmptySynthDrafts();
  scanResultBuilderCursorReportsMalformedFields();
}

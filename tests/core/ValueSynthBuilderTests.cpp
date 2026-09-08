/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/RecordReader.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/scan/ScanResultBuilder.h"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace vgmtrans::core;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool hasLink(const SourceAnnotation& annotation, SourceLinkRole role, const SourceTarget& target) {
  return std::ranges::any_of(annotation.links,
                             [&](const SourceLink& link) { return link.role == role && link.target == target; });
}

const SourceField* fieldNamed(const SourceAnnotation& annotation, std::string_view name) {
  const auto found = std::ranges::find(annotation.fields, name, &SourceField::name);
  return found == annotation.fields.end() ? nullptr : &*found;
}

bool unsignedFieldEquals(const SourceAnnotation& annotation, std::string_view name, u64 expected) {
  const SourceField* field = fieldNamed(annotation, name);
  const auto* value = field == nullptr ? nullptr : std::get_if<u64>(&field->value);
  return value != nullptr && *value == expected;
}

void envelopeAnnotationsPreservePhysicalValues() {
  SourceMapBuilder sourceMap;
  const SourceRange range{.source = SourceId{3}, .offset = 10, .size = 4};
  const auto empty = sourceMap.annotation(SourceRole::Region, "Empty", range);
  annotateSynthValue(empty, Region{});
  const auto timed = sourceMap.annotation(SourceRole::Region, "Timed", range);
  annotateSynthValue(timed, Region{.envelope = Envelope{
                                       .attackSeconds = 0.0,
                                       .holdSeconds = std::numeric_limits<double>::infinity(),
                                       .decaySeconds = 1.25,
                                       .secondDecaySeconds = std::numeric_limits<double>::infinity(),
                                       .releaseSeconds = 2.5,
                                       .sustainAmplitude = 0.0,
                                   }});
  const auto map = sourceMap.finish();
  const auto& emptyFields = map.get(empty.id()).fields;
  const auto& fields = map.get(timed.id()).fields;
  const std::vector<std::pair<std::string, SourceValue>> expected{
      {"attack_seconds", 0.0},         {"hold_infinite", true}, {"decay_seconds", 1.25},
      {"second_decay_infinite", true}, {"sustain_level", 0.0},  {"release_seconds", 2.5},
  };
  expect(fields.size() == emptyFields.size() + expected.size(),
         "absent envelope stages must not produce derived fields");
  for (size_t index = 0; index < expected.size(); ++index) {
    const auto& field = fields[emptyFields.size() + index];
    expect(field.name == expected[index].first && field.value == expected[index].second && !field.range.valid() &&
               field.display == (index == 4 ? SourceValueDisplay::Percent : SourceValueDisplay::Default),
           "envelope annotations must retain stage order, zero and infinite values, and sustain display units");
  }
}

void recordReaderFinishesOnePortableSourceValue() {
  const SourceId source{29};
  const std::vector<u8> bytes{0, 0, 0, 0, 0x12, 0x34, 0, 0, 0x78, 0x56, 0, 0};
  RecordReader reader(ByteReader(source, bytes), 4, 12);
  expect(*reader.u16leAt(4, "later") == 0x5678 && *reader.u16leAt(0, "first") == 0x3412,
         "positioned record reads should express fixed layouts without manual address arithmetic");
  const SourceRecord record = std::move(reader).finish();
  expect(record.range == SourceRange{.source = source, .offset = 4, .size = 8} && record.fields.size() == 2 &&
             record.fields[0].range.offset == 8 && record.fields[1].range.offset == 4,
         "a finished source record should keep one covering range and every exact field range");
}

void recordReaderPreservesNumericFieldsAndFailurePolicies() {
  const SourceId source{29};
  const std::vector<u8> bytes{0, 0xfe, 0xdc, 0xba, 0x98};
  const auto check = [&](auto sequential, auto positioned, auto expected, u32 width) {
    RecordReader stream(ByteReader(source, bytes), 1, 5);
    RecordReader record(ByteReader(source, bytes), 1, 5);
    const auto first = (stream.*sequential)("value", SourceValueDisplay::Hex);
    const auto second = (record.*positioned)(0, "value", SourceValueDisplay::Hex);
    const SourceRange range{.source = source, .offset = 1, .size = width};
    expect(first && second && first.value == expected && second.value == expected &&
               first.range == range && second.range == range && stream.position() == 1 + width &&
               record.position() == 1 + width,
           "numeric record reads must preserve signedness, byte order, field ranges, and cursor position");
    const SourceRecord fields = std::move(record).finish();
    expect(fields.fields.size() == 1 && fields.fields[0].value == makeSourceValue(expected) &&
               fields.fields[0].display == SourceValueDisplay::Hex,
           "record fields must retain their numeric type and requested display style");
  };
  check(&RecordReader::u8, &RecordReader::u8At, u8{0xfe}, 1);
  check(&RecordReader::s8, &RecordReader::s8At, s8{-2}, 1);
  check(&RecordReader::u16be, &RecordReader::u16beAt, u16{0xfedc}, 2);
  check(&RecordReader::u16le, &RecordReader::u16leAt, u16{0xdcfe}, 2);
  check(&RecordReader::s16be, &RecordReader::s16beAt, s16{-292}, 2);
  check(&RecordReader::s16le, &RecordReader::s16leAt, s16{-8962}, 2);
  check(&RecordReader::u32be, &RecordReader::u32beAt, u32{0xfedcba98}, 4);
  check(&RecordReader::u32le, &RecordReader::u32leAt, u32{0x98badcfe}, 4);

  std::vector<Diagnostic> diagnostics;
  RecordReader damaged(ByteReader(source, bytes), 1, 4, &diagnostics);
  expect(!damaged.u32be("too wide") && !damaged.u8("after failure") && damaged.position() == 4,
         "a truncated sequential field must consume available bytes and stop later sequential reads");
  expect(*damaged.s16beAt(0, "recoverable") == -292 && !damaged.ok() && diagnostics.size() == 1 &&
             diagnostics[0].range == SourceRange{.source = source, .offset = 1, .size = 3},
         "positioned reads may recover complete fields without clearing failure or duplicating diagnostics");
}

void brrCatalogProjectsInstrumentsInSampleOrder() {
  struct Patch {
    u8 srcn;
  };
  const std::vector<Patch> patches{{2}, {3}, {1}, {2}};
  std::vector<u8> bytes(0x80);
  bytes[4] = 0x40;
  bytes[8] = 0x50;
  bytes[12] = bytes[13] = 0xff;   // Invalid sample address.
  bytes[0x40] = bytes[0x50] = 1;  // Complete, non-looping BRR blocks.
  const SourceId source{29};
  const auto catalog = readSnesBrrCatalog(ByteReader(source, bytes), 0, patches, &Patch::srcn);
  expect(catalog.samples.size() == 2 && catalog.samples[0].srcn == 1 && catalog.samples[1].srcn == 2,
         "instrument projection must retain sorted unique sample numbers and reject invalid streams");
  expect(catalog.directoryRange == SourceRange{.source = source, .offset = 4, .size = 8} &&
             catalog.samples[0].stream.encodedData == SourceRange{.source = source, .offset = 0x40, .size = 9},
         "projected instruments must retain exact directory and payload source ranges");
  const auto checkRange = [&](auto&& srcns) {
    const auto ranged = readSnesBrrCatalog(ByteReader(source, bytes), 0, std::forward<decltype(srcns)>(srcns));
    expect(ranged.samples.size() == 2 && ranged.samples[0].srcn == 1 && ranged.samples[1].srcn == 2 &&
               ranged.directoryRange == catalog.directoryRange,
           "sample-number ranges must use the same ordering, deduplication, and validation as projected records");
  };
  std::vector<u8> srcns{2, 3, 1, 2};
  checkRange(srcns);
  checkRange(std::as_const(srcns));
  checkRange(std::span<const u8>{srcns});
  checkRange(std::set<u8>{1, 2, 3});
  checkRange(std::views::iota(1, 4));
  expect(srcns == std::vector<u8>({2, 3, 1, 2}), "catalog construction must not reorder the caller's sample numbers");
  checkRange(std::move(srcns));
  const auto empty = readSnesBrrCatalog(ByteReader{}, 0, std::vector<Patch>{}, [](const Patch&) -> u8 {
    throw std::logic_error("an empty range must not evaluate the SRCN projection");
  });
  expect(empty.samples.empty() && !empty.directoryRange.valid(),
         "empty input must produce an empty catalog without reading the source");
}

void brrAliasesRetainLoopIdentityAndSeparateSourceRecords() {
  const SourceId source{29};
  const AssetId asset{40};
  std::vector<u8> bytes(0x80);
  for (u8 srcn = 1; srcn <= 3; ++srcn) {
    bytes[srcn * 4] = 0x40;
    bytes[srcn * 4 + 2] = srcn == 1 ? 0x40 : 0x49;
  }
  bytes[0x49] = 3;  // Two BRR blocks, with a looping end block.
  const ByteReader reader(source, bytes);
  const std::vector<u8> srcns{3, 1, 2};
  const auto catalog = readSnesBrrCatalog(reader, 0, srcns);
  SourceMapBuilder sourceMap;
  SamplePoolBuilder samples(asset, &sourceMap);
  const auto refs = addSnesBrrSamples(samples, reader, catalog);
  const auto built = std::move(samples).finish();
  const auto annotations = sourceMap.finish();
  expect(refs.findSrcn(1) && refs.findSrcn(2) && refs.findSrcn(3) && !refs.findSrcn(4) &&
             refs.findSrcn(1)->index() == 0 && refs.findSrcn(2)->index() == 1 && refs.findSrcn(3)->index() == 1 &&
             refs.findSrcn(3)->owner() == asset,
         "aliases must share a concrete reference only when both BRR data and loop position match");
  expect(built.value.samples.size() == 3 && built.value.samples[0].loop.start == 0 &&
             built.value.samples[1].loop.start == 16 && built.value.samples[2].loop.start == 16,
         "aliased directory entries must retain their individual sample values");
  for (u32 index = 0; index < 3; ++index) {
    const auto sources = annotations.ownedBy(ObjectRefs::sample(asset, index));
    expect(sources.size() == 2 && annotations.get(sources[0]).range == reader.range((index + 1) * 4, 4) &&
               annotations.get(sources[1]).parent == sources[0],
           "each SRCN must retain its own directory annotation and payload child");
  }
}

void sampleBuilderKeepsKeysDenseAndAnnotationsOwned() {
  const SourceId source{30};
  const AssetId asset{40};
  SourceMapBuilder sourceMap;
  std::vector<Diagnostic> diagnostics;
  SamplePoolBuilder samples(asset, &sourceMap, &diagnostics);
  const SourceRange directory{.source = source, .offset = 8, .size = 16};
  samples.include(directory);
  const auto root = samples.source(SourceRole::Table, "Sample Table", directory, "probe-sample-table");

  auto first = samples.add(7, Sample{
                                  .name = "First",
                                  .encodedData = SourceRange{.source = source, .offset = 100, .size = 9},
                              });
  expect(first.ref().index() == 0, "first sample source key should receive dense index zero");
  const SourceRecord firstRecord{
      .range = SourceRange{.source = source, .offset = 8, .size = 4},
      .fields = {SourceField{
          .name = "srcn",
          .range = SourceRange{.source = source, .offset = 8, .size = 1},
          .value = makeSourceValue(u8{7}),
          .display = SourceValueDisplay::Hex,
      }},
  };
  first.source("First Entry", firstRecord, "probe-sample-entry").parent(root.id()).outline(SourceOutlinePolicy::Show);
  first.source("Additional Entry", SourceRange{.source = source, .offset = 12, .size = 4}, "probe-sample-entry")
      .parent(root.id());

  auto second = samples.add(20, Sample{
                                    .name = "Fallback",
                                    .encodedData = SourceRange{.source = source, .offset = 200, .size = 18},
                                });
  expect(second.ref().index() == 1, "a sparse source key should still receive the next dense index");
  expect(!samples.add(7, Sample{}), "a duplicate source key should not return a usable entry");
  expect(samples.size() == 2, "rejected sample keys must not change later dense indexes");
  expect(samples.range() == directory, "an included table range should remain the asset's primary range");
  const auto firstRef = samples.find(7);
  const auto secondRef = samples.find(20);
  expect(firstRef && firstRef->owner() == asset && firstRef->index() == 0 && secondRef && secondRef->owner() == asset &&
             secondRef->index() == 1 && !samples.find(99),
         "sample lookup should resolve sparse keys to dense references and reject missing keys");

  const auto built = std::move(samples).finish();
  const auto& collection = built.value;
  const SourceMap annotations = sourceMap.finish();
  expect(built.range == directory, "finish should return the final sample collection range");
  expect(collection.samples.size() == 2, "sample builder should finish ordinary sample values");
  expect(firstRef && collection.samples[firstRef->index()].name == "First" && secondRef &&
             collection.samples[secondRef->index()].name == "Fallback",
         "concrete sample references should retain their meaning after finalization");
  expect(diagnostics.size() == 1 && diagnostics[0].code == "synth.sample-key.duplicate",
         "sample builder should diagnose a duplicate key once");

  const auto firstSources = annotations.ownedBy(ObjectRefs::sample(asset, 0));
  expect(firstSources.size() == 2, "additional source records should retain the same sample owner");
  const SourceAnnotation& firstAnnotation = annotations.get(firstSources[0]);
  const SourceField* srcn = fieldNamed(firstAnnotation, "srcn");
  expect(firstAnnotation.outline == SourceOutlinePolicy::Show && srcn != nullptr &&
             srcn->range == SourceRange{.source = source, .offset = 8, .size = 1} &&
             srcn->display == SourceValueDisplay::Hex && unsignedFieldEquals(firstAnnotation, "channels", 1) &&
             unsignedFieldEquals(firstAnnotation, "bits_per_sample", 16) &&
             unsignedFieldEquals(firstAnnotation, "effective_sample_rate", 0),
         "synth source records should retain field ranges, display hints, and outline presentation for future views");
  const auto fallbackSources = annotations.ownedBy(ObjectRefs::sample(asset, 1));
  expect(fallbackSources.size() == 1 && annotations.get(fallbackSources[0]).range == collection.samples[1].encodedData,
         "a source-backed sample without source() should receive a generic payload annotation");
  expect(annotations.ownedBy(ObjectRefs::asset(asset)) == std::vector<SourceAnnotationId>{root.id()},
         "asset-level source structures should receive the sample collection owner automatically");
}

void instrumentBuilderGroupsEntriesAndProjectsRegionIdentity() {
  const SourceId source{31};
  const AssetId instrumentsAsset{50};
  const AssetId samplesAsset{51};
  SourceMapBuilder sourceMap;
  std::vector<Diagnostic> diagnostics;
  InstrumentSetBuilder instruments(instrumentsAsset, &sourceMap, &diagnostics);
  const SourceRange table{.source = source, .offset = 0, .size = 80};
  instruments.include(table);

  auto kit = instruments.getOrAdd(
      700, Instrument{.explicitAddress = InstrumentAddress{.bank = 127, .program = 5}, .name = "Drum Kit"});
  auto firstRegion = kit.region(SampleRef::resolved(samplesAsset, 3),
                                Region{.keyRange = KeyRange{.low = 36, .high = 36}});
  const auto firstRegionSource =
      firstRegion.source("Kick",
                         SourceRecord{
                             .range = SourceRange{.source = source, .offset = 20, .size = 4},
                             .fields = {SourceField{
                                 .name = "sample",
                                 .range = SourceRange{.source = source, .offset = 20, .size = 1},
                                 .value = makeSourceValue(u8{3}),
                             }},
                         },
                         "probe-kick");
  firstRegion.source("Kick Tuning", SourceRange{.source = source, .offset = 60, .size = 2}, "probe-kick-tuning");

  const auto instrumentSource =
      kit.source("Drum Kit", SourceRange{.source = source, .offset = 16, .size = 8}, "probe-drum-kit");
  const auto latestInstrumentSource =
      kit.source("Kit Mapping", SourceRange{.source = source, .offset = 16, .size = 8}, "probe-kit-mapping");
  auto secondRegion = kit.region(SampleRef::resolved(samplesAsset, 4), Region{});
  const auto secondRegionSource =
      secondRegion.source("Snare", SourceRange{.source = source, .offset = 24, .size = 4}, "probe-snare");

  auto sameKit = instruments.getOrAdd(700, Instrument{.name = "Ignored Replacement"});
  expect(sameKit.value().name == "Drum Kit" && instruments.size() == 1,
         "getOrAdd should preserve the first aggregate while grouping later entries");
  expect(!instruments.add(700, Instrument{.name = "Duplicate"}),
         "add should reject a grouping key that already exists");

  instruments.add(900, Instrument{
                           .name = "Sparse",
                           .range = SourceRange{.source = source, .offset = 40, .size = 8},
                           .regions = {Region{
                               .sample = SampleRef::resolved(samplesAsset, 8),
                               .range = SourceRange{.source = source, .offset = 42, .size = 2},
                           }},
                       });
  instruments.append(Instrument{.name = "Derived", .regions = {Region{}}});

  expect(instruments.range() == table, "an explicit instrument table should remain the asset's primary range");
  const auto built = std::move(instruments).finish();
  const auto& values = built.values;
  const SourceMap annotations = sourceMap.finish();
  expect(built.range == table, "finish should return the final instrument set range");
  expect(values.size() == 3 && values[0].regions.size() == 2,
         "instrument builder should finish grouped, sparse, and appended ordinary values");
  expect(values[0].range == SourceRange{.source = source, .offset = 16, .size = 8},
         "instrument source records should supply a missing durable range");
  expect(values[0].regions[0].range == SourceRange{.source = source, .offset = 20, .size = 42},
         "disjoint region records should conservatively cover the durable region range");
  expect(diagnostics.size() == 1 && diagnostics[0].code == "synth.instrument-key.duplicate",
         "instrument add should report a duplicate grouping key once");

  const auto instrumentSources = annotations.ownedBy(ObjectRefs::instrument(instrumentsAsset, 0));
  expect(instrumentSources == std::vector<SourceAnnotationId>{instrumentSource.id(), latestInstrumentSource.id()},
         "instrument annotations should use the dense model index rather than the grouping key");
  const SourceAnnotation& instrumentAnnotation = annotations.get(instrumentSource.id());
  expect(hasLink(instrumentAnnotation, SourceLinkRole::UsesSample, SourceTarget{ObjectRefs::sample(samplesAsset, 3)}) &&
             hasLink(instrumentAnnotation, SourceLinkRole::UsesSample,
                     SourceTarget{ObjectRefs::sample(samplesAsset, 4)}) &&
             instrumentAnnotation.links.size() == 2,
         "instrument sample links should stay complete and unique regardless of call order");
  expect(unsignedFieldEquals(instrumentAnnotation, "bank", 127) &&
             unsignedFieldEquals(instrumentAnnotation, "program", 5) &&
             unsignedFieldEquals(instrumentAnnotation, "region_count", 2) &&
             unsignedFieldEquals(annotations.get(firstRegionSource.id()), "key_low", 36),
         "builder finish should project final synth properties without format-authored annotation bookkeeping");

  const auto firstRegionSources = annotations.ownedBy(ObjectRefs::region(instrumentsAsset, 0, 0));
  expect(firstRegionSources.size() == 2 && annotations.get(firstRegionSource.id()).parent == std::nullopt &&
             annotations.get(firstRegionSource.id()).fieldsAsChildren,
         "region records should share stable ownership, avoid guessed parents, and expose exact fields as children");
  const auto secondRegionSources = annotations.ownedBy(ObjectRefs::region(instrumentsAsset, 0, 1));
  expect(secondRegionSources == std::vector<SourceAnnotationId>{secondRegionSource.id()} &&
             annotations.get(secondRegionSource.id()).parent == latestInstrumentSource.id(),
         "region sources should inherit the most recently added instrument source parent");
  expect(hasLink(annotations.get(secondRegionSource.id()), SourceLinkRole::UsesSample,
                 SourceTarget{ObjectRefs::sample(samplesAsset, 4)}),
         "a region source should link to its exact concrete sample");

  expect(annotations.ownedBy(ObjectRefs::instrument(instrumentsAsset, 1)).size() == 1 &&
             annotations.ownedBy(ObjectRefs::region(instrumentsAsset, 1, 0)).size() == 1,
         "pre-populated source-backed instruments and regions should receive generic annotations");
  expect(annotations.ownedBy(ObjectRefs::instrument(instrumentsAsset, 2)).empty() &&
             annotations.ownedBy(ObjectRefs::region(instrumentsAsset, 2, 0)).empty(),
         "genuinely derived values without ranges should not receive fabricated annotations");
}

void soundBankOwnsNoncontiguousSamplesWithoutInventingOneSourceRange() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "noncontiguous-bank.probe"}, std::vector<u8>(256));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "SynthBuilderProbe");
  const SourceRange instrumentTable = input.reader.range(8, 8);
  const SourceRange sampleData = input.reader.range(192, 9);
  auto bank = result.soundBank("Noncontiguous Bank", instrumentTable);
  const auto sample = bank.localSamples().add(4, Sample{.name = "Local Sample", .encodedData = sampleData});
  bank.instruments().add(0, Instrument{.name = "Instrument"}).region(sample.ref(), Region{});

  const ScanResult scan = result.finish();
  const auto* soundBank = std::get_if<SoundBankAsset>(&scan.assets.front());
  expect(scan.assets.size() == 1 && soundBank != nullptr,
         "a bank and its local samples should publish as one asset");
  expect(soundBank->metadata.range == instrumentTable &&
             soundBank->localSamples.samples.front().encodedData == sampleData,
         "bank metadata may keep its primary table range while each noncontiguous sample keeps its exact range");
  expect(soundBank->instruments.front().regions.front().sample.owner() == soundBank->metadata.id,
         "a sample produced by a sound bank should remain explicitly local to that bank");
  expect(!scan.sourceMap.ownedBy(ObjectRefs::sample(soundBank->metadata.id, 0)).empty(),
         "local sample provenance should use the owning sound bank identity");
}

void scanResultBuilderOwnsSynthDraftsUntilFinish() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "synth-builder.probe"}, std::vector<u8>(64));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "SynthBuilderProbe");
  auto bank = result.soundBank("Probe Instruments", input.reader.range(8, 8));
  auto pool = result.samplePool("Probe Samples", input.reader.range(0, 8));
  auto& instruments = bank.instruments();
  auto& samples = pool.samples();
  const AssetId instrumentAssetId = bank.id();
  const AssetId sampleAssetId = pool.id();

  const auto concreteSample = samples
                                  .add(12,
                                       Sample{
                                           .name = "Probe Sample",
                                           .encodedData = input.reader.range(32, 9),
                                       })
                                  .ref();

  auto instrument = instruments.add(90, Instrument{.name = "Probe Instrument"});
  instrument.source("Probe Instrument", input.reader.range(8, 4));
  instrument.region(concreteSample, Region{}).source("Region", input.reader.range(12, 4));

  const ScanResult scan = result.finish();

  expect(scan.assets.size() == 2, "finish should materialize the two result-owned synth drafts");
  const auto* instrumentAsset = std::get_if<SoundBankAsset>(&scan.assets[0]);
  const auto* sampleAsset = std::get_if<SamplePoolAsset>(&scan.assets[1]);
  expect(instrumentAsset != nullptr && sampleAsset != nullptr,
         "draft creation order should determine materialized asset order");
  expect(
      instrumentAsset->metadata.id == instrumentAssetId && instrumentAsset->metadata.range == input.reader.range(8, 8),
      "instrument materialization should use the draft's stable id and accumulated range");
  expect(sampleAsset->metadata.id == sampleAssetId && sampleAsset->metadata.range == input.reader.range(0, 8),
         "sample materialization should use the draft's stable id and included range");
  expect(instrumentAsset->instruments[0].regions[0].sample.owner() == sampleAssetId,
         "concrete sample references should survive the finish boundary");
  expect(!scan.sourceMap.ownedBy(ObjectRefs::region(instrumentAssetId, 0, 0)).empty(),
         "scan-time builders should publish stable region ownership into the finished source map");
}

void scanResultBuilderRetainsSampleKeysAndExposesExistingRegions() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "synth-lookup.probe"}, std::vector<u8>(64));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "SynthBuilderProbe");

  auto pool = result.samplePool("Sparse Samples");
  auto& samples = pool.samples();
  const AssetId samplesAsset = pool.id();
  samples.add(12, Sample{.name = "Sparse Sample", .encodedData = input.reader.range(32, 4)});
  const auto sample = samples.find(12);
  expect(sample && sample->owner() == samplesAsset && sample->index() == 0,
         "a sample draft should retain sparse keys for later instrument tables");
  if (!samples.find(99)) {
    samples.warning("Required sample 99 was not found", input.reader.range(4, 1));
  }

  auto bank = result.soundBank("Prebuilt Instruments");
  auto& instruments = bank.instruments();
  const AssetId instrumentsAsset = bank.id();
  auto instrument = instruments.add(7, Instrument{
                                           .name = "Prebuilt Instrument",
                                           .range = input.reader.range(8, 8),
                                           .regions = {Region{
                                               .sample = *sample,
                                               .range = input.reader.range(12, 4),
                                           }},
                                       });
  instrument.source("Prebuilt Instrument", input.reader.range(8, 8), "probe-prebuilt-instrument");
  expect(static_cast<bool>(instrument.regionAt(0)),
         "regionAt should expose a region supplied in the ordinary Instrument value");
  instrument.regionAt(0).source("Prebuilt Region", input.reader.range(12, 4), "probe-prebuilt-region");
  expect(!instrument.regionAt(1), "regionAt should reject an index outside the prebuilt region vector");
  const ScanResult scan = result.finish();
  expect(scan.assets.size() == 2 && metadata(scan.assets[0]).id == samplesAsset &&
             metadata(scan.assets[1]).id == instrumentsAsset,
         "result-owned drafts should materialize ordinary assets with stable IDs");
  expect(scan.diagnostics.size() == 1 && scan.diagnostics[0].message == "Required sample 99 was not found",
         "a draft should report a format-authored warning through the shared diagnostic stream");
  const auto regionSources = scan.sourceMap.ownedBy(ObjectRefs::region(instrumentsAsset, 0, 0));
  expect(regionSources.size() == 1 && scan.sourceMap.get(regionSources[0]).category() == "probe-prebuilt-region",
         "a prebuilt region should accept an exact source record without being removed and added again");
}

void entryValuesAreReadOnlyAndInitialRangesRemainAuthoritative() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "synth-read-only-entry.probe"}, std::vector<u8>(64));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "SynthBuilderProbe");
  auto bank = result.soundBank("Late Instruments");
  auto pool = result.samplePool("Late Samples");
  auto& instruments = bank.instruments();
  auto& samples = pool.samples();
  const AssetId instrumentAssetId = bank.id();
  const AssetId sampleAssetId = pool.id();

  auto sample = samples.add(0, Sample{.name = "Sample", .encodedData = input.reader.range(32, 9)});
  auto instrument = instruments.add(0, Instrument{.name = "Instrument", .range = input.reader.range(8, 4)});
  instrument.source("Instrument", input.reader.range(8, 4));
  auto region = instrument.region(sample.ref(), Region{.range = input.reader.range(12, 4)});
  region.source("Region", input.reader.range(12, 4));

  static_assert(std::is_same_v<decltype(sample.value()), const Sample&>);
  static_assert(std::is_same_v<decltype(instrument.value()), const Instrument&>);
  static_assert(std::is_same_v<decltype(region.value()), const Region&>);

  const ScanResult scan = result.finish();
  const auto* instrumentAsset = std::get_if<SoundBankAsset>(&scan.assets[0]);
  const auto* sampleAsset = std::get_if<SamplePoolAsset>(&scan.assets[1]);
  expect(instrumentAsset != nullptr && instrumentAsset->metadata.range == input.reader.range(8, 8),
         "instrument and region ranges should contribute to final asset metadata when inserted");
  expect(sampleAsset != nullptr && sampleAsset->metadata.range == input.reader.range(32, 9),
         "sample payload ranges should contribute to final asset metadata when inserted");
  expect(instrumentAsset->instruments[0].range == input.reader.range(8, 4) &&
             instrumentAsset->instruments[0].regions[0].range == input.reader.range(12, 4),
         "source records should not replace explicit durable ranges");
  expect(!scan.sourceMap.ownedBy(ObjectRefs::instrument(instrumentAssetId, 0)).empty() &&
             !scan.sourceMap.ownedBy(ObjectRefs::region(instrumentAssetId, 0, 0)).empty() &&
             !scan.sourceMap.ownedBy(ObjectRefs::sample(sampleAssetId, 0)).empty(),
         "read-only entry views should retain durable source owners");
}

void scanResultBuilderDraftViewsRemainStableAsTheResultGrows() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "stable-draft.probe"}, std::vector<u8>(64));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "SynthBuilderProbe");
  auto pool = result.samplePool("Stable Samples");
  auto& samples = pool.samples();
  auto sample = samples.add(7, Sample{.name = "Stable Sample", .encodedData = input.reader.range(32, 4)});

  // Growing the result must not invalidate a draft proxy or an entry returned
  // from one of its domain builders.
  for (u32 index = 0; index < 64; ++index) {
    result.misc("Padding", input.reader.range(index, 1)).payload({static_cast<u8>(index)});
  }
  sample.source("Stable Sample", input.reader.range(12, 4), "probe-stable-sample");
  const auto retained = samples.find(7);

  const ScanResult scan = result.finish();
  const auto& sampleAsset = std::get<SamplePoolAsset>(scan.assets.front());
  expect(sampleAsset.pool.samples.size() == 1 && pool.id() == sampleAsset.metadata.id && retained &&
             retained->owner() == pool.id() && retained->index() == 0,
         "draft proxies and sparse lookups should survive growth of the result-owned draft list");
  expect(scan.sourceMap.ownedBy(ObjectRefs::sample(pool.id(), 0)).size() == 1,
         "entries obtained before result growth should still publish their source annotations");
}

void detachedBuildersUseTheSameAuthoringSurface() {
  const SourceId source{32};
  std::vector<Diagnostic> diagnostics;
  SamplePoolBuilder samples(AssetId{60}, nullptr, &diagnostics);
  auto sample = samples.add(4, Sample{
                                   .name = "Detached Sample",
                                   .encodedData = SourceRange{.source = source, .offset = 100, .size = 9},
                               });
  sample.source("Detached Sample", SourceRange{.source = source, .offset = 20, .size = 4})
      .field("srcn", SourceRange{.source = source, .offset = 20, .size = 1}, u8{4});
  const SampleRef concreteSample = sample.ref();

  InstrumentSetBuilder instruments(AssetId{61}, nullptr, &diagnostics);
  auto instrument = instruments.add(9, Instrument{.name = "Detached Instrument"});
  instrument.source("Detached Instrument", SourceRange{.source = source, .offset = 40, .size = 4})
      .derived("program", 9);
  instrument.region(concreteSample, Region{})
      .source("Detached Region", SourceRange{.source = source, .offset = 44, .size = 4});

  const auto sampleValues = std::move(samples).finish();
  const auto instrumentValues = std::move(instruments).finish();
  expect(sampleValues.value.samples.size() == 1 && instrumentValues.values.size() == 1 &&
             instrumentValues.values[0].regions.size() == 1,
         "detached builders should finish ordinary values through the scan-time vocabulary");
  expect(instrumentValues.values[0].range == SourceRange{.source = source, .offset = 40, .size = 4} &&
             instrumentValues.values[0].regions[0].range == SourceRange{.source = source, .offset = 44, .size = 4},
         "detached source calls should still accumulate durable object ranges");
  expect(diagnostics.empty(), "valid detached construction should not report diagnostics");
}

}  // namespace

void runValueSynthBuilderTests() {
  envelopeAnnotationsPreservePhysicalValues();
  recordReaderFinishesOnePortableSourceValue();
  recordReaderPreservesNumericFieldsAndFailurePolicies();
  brrCatalogProjectsInstrumentsInSampleOrder();
  brrAliasesRetainLoopIdentityAndSeparateSourceRecords();
  sampleBuilderKeepsKeysDenseAndAnnotationsOwned();
  instrumentBuilderGroupsEntriesAndProjectsRegionIdentity();
  soundBankOwnsNoncontiguousSamplesWithoutInventingOneSourceRange();
  scanResultBuilderOwnsSynthDraftsUntilFinish();
  scanResultBuilderRetainsSampleKeysAndExposesExistingRegions();
  entryValuesAreReadOnlyAndInitialRangesRemainAuthoritative();
  scanResultBuilderDraftViewsRemainStableAsTheResultGrows();
  detachedBuildersUseTheSameAuthoringSurface();
}

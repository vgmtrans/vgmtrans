/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/RecordReader.h"
#include "value/scan/ScanResultBuilder.h"

#include <algorithm>
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
  auto alias = samples.alias(9, 7);
  alias.source("Alias Entry", SourceRange{.source = source, .offset = 12, .size = 4}, "probe-sample-alias")
      .parent(root.id());

  auto second = samples.add(20, Sample{
                                    .name = "Fallback",
                                    .encodedData = SourceRange{.source = source, .offset = 200, .size = 18},
                                });
  expect(second.ref().index() == 1, "a sparse source key should still receive the next dense index");
  expect(!samples.add(7, Sample{}), "a duplicate source key should not return a usable entry");
  expect(!samples.alias(11, 99), "an alias to a missing key should not return a usable entry");
  expect(samples.size() == 2, "rejected sample keys must not change later dense indexes");
  expect(samples.range() == directory, "an included table range should remain the asset's primary range");

  const auto built = std::move(samples).finish();
  const auto& collection = built.value;
  const auto& retained = built.refs;
  const SourceMap annotations = sourceMap.finish();
  expect(built.range == directory, "finish should return the final sample collection range");
  expect(collection.samples.size() == 2, "sample builder should finish ordinary sample values");
  expect(retained.find(7) && retained.find(7)->index() == 0 && retained.find(9) && retained.find(9)->index() == 0,
         "retained lookup should preserve direct and alias mappings after finish");
  expect(retained.find(20) && retained.find(20)->index() == 1,
         "retained lookup should preserve sparse source keys after finish");
  expect(diagnostics.size() == 2 && diagnostics[0].code == "synth.sample-key.duplicate" &&
             diagnostics[1].code == "synth.sample-alias.missing-target",
         "sample builder should diagnose duplicate keys and missing aliases once");

  const auto firstSources = annotations.ownedBy(ObjectRefs::sample(asset, 0));
  expect(firstSources.size() == 2, "an alias should add provenance to the existing sample rather than duplicate it");
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
  expect(instrumentSources == std::vector<SourceAnnotationId>{instrumentSource.id()},
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
             annotations.get(secondRegionSource.id()).parent == instrumentSource.id(),
         "region sources added after an instrument source should inherit that source parent");
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
  const auto sample = bank.samples().add(4, Sample{.name = "Local Sample", .encodedData = sampleData});
  bank.add(0, Instrument{.name = "Instrument"}).region(sample.ref(), Region{});

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
  auto instruments = result.soundBank("Probe Instruments", input.reader.range(8, 8));
  auto samples = result.samplePool("Probe Samples", input.reader.range(0, 8));
  const AssetId instrumentAssetId = instruments.id();
  const AssetId sampleAssetId = samples.id();

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

  auto samples = result.samplePool("Sparse Samples");
  const AssetId samplesAsset = samples.id();
  samples.add(12, Sample{.name = "Sparse Sample", .encodedData = input.reader.range(32, 4)});
  samples.alias(20, 12);
  const auto sample = samples.find(20);
  expect(sample && sample->owner() == samplesAsset && sample->index() == 0,
         "a sample draft should retain sparse and alias keys for later instrument tables");
  if (!samples.find(99)) {
    samples.warning("Required sample 99 was not found", input.reader.range(4, 1));
  }

  auto instruments = result.soundBank("Prebuilt Instruments");
  const AssetId instrumentsAsset = instruments.id();
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
  expect(regionSources.size() == 1 && scan.sourceMap.get(regionSources[0]).localKind == "probe-prebuilt-region",
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
  auto instruments = result.soundBank("Late Instruments");
  auto samples = result.samplePool("Late Samples");
  const AssetId instrumentAssetId = instruments.id();
  const AssetId sampleAssetId = samples.id();

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
  auto samples = result.samplePool("Stable Samples");
  auto sample = samples.add(7, Sample{.name = "Stable Sample", .encodedData = input.reader.range(32, 4)});

  // Growing the result must not invalidate a draft proxy or an entry returned
  // from one of its domain builders.
  for (u32 index = 0; index < 64; ++index) {
    result.misc("Padding", input.reader.range(index, 1)).payload({static_cast<u8>(index)});
  }
  samples.alias(9, 7);
  sample.source("Stable Sample", input.reader.range(12, 4), "probe-stable-sample");
  const auto alias = samples.find(9);

  const ScanResult scan = result.finish();
  const auto& sampleAsset = std::get<SamplePoolAsset>(scan.assets.front());
  expect(sampleAsset.pool.samples.size() == 1 && samples.id() == sampleAsset.metadata.id && alias &&
             alias->owner() == samples.id() && alias->index() == 0,
         "draft proxies and sparse lookups should survive growth of the result-owned draft list");
  expect(scan.sourceMap.ownedBy(ObjectRefs::sample(samples.id(), 0)).size() == 1,
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
  recordReaderFinishesOnePortableSourceValue();
  sampleBuilderKeepsKeysDenseAndAnnotationsOwned();
  instrumentBuilderGroupsEntriesAndProjectsRegionIdentity();
  soundBankOwnsNoncontiguousSamplesWithoutInventingOneSourceRange();
  scanResultBuilderOwnsSynthDraftsUntilFinish();
  scanResultBuilderRetainsSampleKeysAndExposesExistingRegions();
  entryValuesAreReadOnlyAndInitialRangesRemainAuthoritative();
  scanResultBuilderDraftViewsRemainStableAsTheResultGrows();
  detachedBuildersUseTheSameAuthoringSurface();
}

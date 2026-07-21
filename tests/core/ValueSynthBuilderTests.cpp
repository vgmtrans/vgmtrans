/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/RecordReader.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/CollectionPreparation.h"
#include "value/validation/SnapshotValidation.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
  SampleCollectionBuilder samples(asset, &sourceMap, &diagnostics);
  const SourceRange directory{.source = source, .offset = 8, .size = 16};
  samples.include(directory);
  const auto root = samples.source(SourceRole::Table, "Sample Table", directory, "probe-sample-table");

  auto first = samples.add(7, Sample{
                                  .name = "First",
                                  .encodedData = SourceRange{.source = source, .offset = 100, .size = 9},
                              });
  expect(first.ref().index == 0, "first sample source key should receive dense index zero");
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
  expect(second.ref().index == 1, "a sparse source key should still receive the next dense index");
  const SampleRefLookup retained = samples.refs();

  expect(!samples.add(7, Sample{}), "a duplicate source key should not return a usable entry");
  expect(!samples.alias(11, 99), "an alias to a missing key should not return a usable entry");
  expect(samples.size() == 2, "rejected sample keys must not change later dense indexes");
  expect(samples.range() == directory, "an included table range should remain the asset's primary range");

  const SampleCollection collection = std::move(samples).finish();
  const SourceMap annotations = sourceMap.finish();
  expect(collection.samples.size() == 2, "sample builder should finish ordinary sample values");
  expect(retained.find(7) && retained.find(7)->index == 0 && retained.find(9) && retained.find(9)->index == 0,
         "retained lookup should preserve direct and alias mappings after finish");
  expect(retained.find(20) && retained.find(20)->index == 1,
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
  auto firstRegion = kit.region(SampleRef{.collection = samplesAsset, .index = 3},
                                Region{.keyRange = KeyRange{.low = 36, .high = 36}});
  const auto firstRegionSource =
      firstRegion.source("Kick", SourceRange{.source = source, .offset = 20, .size = 4}, "probe-kick");
  firstRegion.source("Kick Tuning", SourceRange{.source = source, .offset = 60, .size = 2}, "probe-kick-tuning");

  const auto instrumentSource =
      kit.source("Drum Kit", SourceRange{.source = source, .offset = 16, .size = 8}, "probe-drum-kit");
  auto secondRegion = kit.region(SampleRef{.collection = samplesAsset, .index = 4}, Region{});
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
                               .sample = SampleRef{.collection = samplesAsset, .index = 8},
                               .range = SourceRange{.source = source, .offset = 42, .size = 2},
                           }},
                       });
  instruments.append(Instrument{.name = "Derived", .regions = {Region{}}});

  expect(instruments.range() == table, "an explicit instrument table should remain the asset's primary range");
  const auto values = std::move(instruments).finish();
  const SourceMap annotations = sourceMap.finish();
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
  expect(firstRegionSources.size() == 2 && annotations.get(firstRegionSource.id()).parent == std::nullopt,
         "several source records should share one stable region owner without guessed parentage");
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

void scanResultBuilderCommitsSynthBuildersExplicitly() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "synth-builder.probe"}, std::vector<u8>(64));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "SynthBuilderProbe");
  const auto instrumentRef = result.reserveInstrumentSet();
  const auto sampleRef = result.reserveSampleCollection();

  auto samples = result.samples(sampleRef);
  samples.include(input.reader.range(0, 8));
  const auto concreteSample = samples
                                  .add(12,
                                       Sample{
                                           .name = "Probe Sample",
                                           .encodedData = input.reader.range(32, 9),
                                       })
                                  .ref();

  auto instruments = result.instruments(instrumentRef);
  instruments.include(input.reader.range(8, 8));
  auto instrument = instruments.add(90, Instrument{.name = "Probe Instrument"});
  instrument.source("Probe Instrument", input.reader.range(8, 4));
  instrument.region(concreteSample, Region{}).source("Region", input.reader.range(12, 4));

  result.instrumentSet("Probe Instruments", std::move(instruments));
  result.sampleCollection("Probe Samples", std::move(samples));
  const ScanResult scan = result.finish();

  expect(scan.assets.size() == 2, "consuming synth-builder commits should add two ordinary assets");
  const auto* instrumentAsset = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  const auto* sampleAsset = std::get_if<SampleCollectionAsset>(&scan.assets[1]);
  expect(instrumentAsset != nullptr && sampleAsset != nullptr,
         "consuming commits should preserve explicit instrument-then-sample asset order");
  expect(
      instrumentAsset->metadata.id == instrumentRef.id && instrumentAsset->metadata.range == input.reader.range(8, 8),
      "instrument commit should use the builder's reserved id and accumulated range");
  expect(sampleAsset->metadata.id == sampleRef.id && sampleAsset->metadata.range == input.reader.range(0, 8),
         "sample commit should use the builder's reserved id and included range");
  expect(instrumentAsset->instruments[0].regions[0].sample.collection == sampleRef.id,
         "concrete sample references should survive the builder commit boundary");
  expect(!scan.sourceMap.ownedBy(ObjectRefs::region(instrumentRef.id, 0, 0)).empty(),
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

  auto samples = result.samples();
  const AssetId samplesAsset = samples.assetId();
  samples.add(12, Sample{.name = "Sparse Sample", .encodedData = input.reader.range(32, 4)});
  samples.alias(20, 12);
  const auto sampleCollection = result.sampleCollection("Sparse Samples", std::move(samples));
  const auto sample = result.sampleByKey(sampleCollection, 20);
  expect(sample && sample->collection == samplesAsset && sample->index == 0,
         "a consumed sample builder should retain sparse and alias keys for later instrument tables");
  expect(!result.sampleByKeyOrWarning(sampleCollection, 99, "Required sample 99", input.reader.range(4, 1)),
         "the shared lookup helper should reject a missing source key");
  expect(!result.sampleByKeyOrWarning(std::nullopt, 12, "Required sample collection", input.reader.range(5, 1)),
         "the shared lookup helper should reject an absent sample collection");

  auto instruments = result.instruments();
  const AssetId instrumentsAsset = instruments.assetId();
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
  result.instrumentSet("Prebuilt Instruments", std::move(instruments));

  const ScanResult scan = result.finish();
  expect(scan.assets.size() == 2 && metadata(scan.assets[0]).id == samplesAsset &&
             metadata(scan.assets[1]).id == instrumentsAsset,
         "auto-reserving synth builders should commit ordinary assets with their reserved IDs");
  expect(scan.diagnostics.size() == 2 && scan.diagnostics[0].message == "Required sample 99 was not found" &&
             scan.diagnostics[1].message == "Required sample collection was not found",
         "the shared missing-sample helper should explain missing keys and collections");
  const auto regionSources = scan.sourceMap.ownedBy(ObjectRefs::region(instrumentsAsset, 0, 0));
  expect(regionSources.size() == 1 && scan.sourceMap.get(regionSources[0]).localKind == "probe-prebuilt-region",
         "a prebuilt region should accept an exact source record without being removed and added again");
}

void valueEscapeHatchContributesFinalRanges() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "synth-value-escape.probe"}, std::vector<u8>(64));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "SynthBuilderProbe");
  const auto instrumentRef = result.reserveInstrumentSet();
  const auto sampleRef = result.reserveSampleCollection();

  auto samples = result.samples(sampleRef);
  auto sample = samples.add(0, Sample{.name = "Late Sample"});
  sample.value().encodedData = input.reader.range(32, 9);

  auto instruments = result.instruments(instrumentRef);
  auto instrument = instruments.add(0, Instrument{.name = "Late Instrument"});
  instrument.value().range = input.reader.range(8, 4);
  instrument.source("Late Instrument", input.reader.range(8, 4));
  auto region = instrument.region(sample.ref(), Region{});
  region.value().range = input.reader.range(12, 4);
  region.source("Late Region", input.reader.range(12, 4));

  result.instrumentSet("Late Instruments", std::move(instruments));
  result.sampleCollection("Late Samples", std::move(samples));
  const ScanResult scan = result.finish();
  const auto* instrumentAsset = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  const auto* sampleAsset = std::get_if<SampleCollectionAsset>(&scan.assets[1]);
  expect(instrumentAsset != nullptr && instrumentAsset->metadata.range == input.reader.range(8, 8),
         "a late instrument or region range should contribute to final asset metadata");
  expect(sampleAsset != nullptr && sampleAsset->metadata.range == input.reader.range(32, 9),
         "a late encoded-data range should contribute to final sample metadata");
  expect(instrumentAsset->instruments[0].range == input.reader.range(8, 4) &&
             instrumentAsset->instruments[0].regions[0].range == input.reader.range(12, 4),
         "source records should not replace durable ranges supplied later through value()");
  expect(!scan.sourceMap.ownedBy(ObjectRefs::instrument(instrumentRef.id, 0)).empty() &&
             !scan.sourceMap.ownedBy(ObjectRefs::region(instrumentRef.id, 0, 0)).empty() &&
             !scan.sourceMap.ownedBy(ObjectRefs::sample(sampleRef.id, 0)).empty(),
         "late source-backed values should still receive generic annotations with durable owners");
}

void detachedBuildersUseTheSameAuthoringSurface() {
  const SourceId source{32};
  std::vector<Diagnostic> diagnostics;
  SampleCollectionBuilder samples(AssetId{60}, nullptr, &diagnostics);
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

  const SampleCollection sampleValues = std::move(samples).finish();
  const auto instrumentValues = std::move(instruments).finish();
  expect(sampleValues.samples.size() == 1 && instrumentValues.size() == 1 && instrumentValues[0].regions.size() == 1,
         "detached builders should finish ordinary values through the scan-time vocabulary");
  expect(instrumentValues[0].range == SourceRange{.source = source, .offset = 40, .size = 4} &&
             instrumentValues[0].regions[0].range == SourceRange{.source = source, .offset = 44, .size = 4},
         "detached source calls should still accumulate durable object ranges");
  expect(diagnostics.empty(), "valid detached construction should not report diagnostics");
}

void collectionPreparationBuildsImmutableSequenceSpecificAssets() {
  const SourceId source{33};
  const AssetId baseInstrumentsId{70};
  const AssetId baseSamplesId{71};
  SessionSnapshotBuilder snapshotBuilder;
  snapshotBuilder.assets.push_back(InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = baseInstrumentsId,
              .format = "Probe",
              .name = "Base Instruments",
              .range = SourceRange{.source = source, .offset = 10, .size = 8},
          },
      .instruments = {Instrument{
          .identity = InstrumentIdentity{.domain = "probe", .key = 3},
          .name = "Base Instrument",
          .range = SourceRange{.source = source, .offset = 10, .size = 4},
          .regions = {Region{
              .sample = SampleRef{.collection = baseSamplesId, .index = 0},
              .range = SourceRange{.source = source, .offset = 11, .size = 1},
          }},
      }},
  });
  snapshotBuilder.assets.push_back(SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = baseSamplesId,
              .format = "Probe",
              .name = "Base Samples",
              .range = SourceRange{.source = source, .offset = 20, .size = 8},
          },
      .samples = SampleCollection{.samples = {Sample{
                                      .name = "Base Sample",
                                      .encodedData = SourceRange{.source = source, .offset = 20, .size = 8},
                                  }}},
  });
  const SessionSnapshot snapshot = snapshotBuilder.finish();
  SourceStore sources;
  ScanIdAllocator ids;
  std::unordered_map<std::string, AssetId> stableSlots;
  u32 nextAsset = 100;

  const auto prepare = [&](std::string key, u8 drumKey) {
    DesiredCollection collection{
        .key = CollectionKey{.resolver = "Probe", .value = std::move(key)},
        .name = "Prepared Probe",
        .instrumentSets = {baseInstrumentsId},
        .sampleCollections = {baseSamplesId},
    };
    MaterializationContext context{
        .sources = sources,
        .snapshot = snapshot,
        .collection = collection,
        .ids = ids,
        .assetIdForSlot =
            [&](std::string_view slot) {
              const std::string stableKey = collection.key.value + ":" + std::string(slot);
              const auto [found, inserted] = stableSlots.try_emplace(stableKey, AssetId{nextAsset});
              if (inserted) {
                ++nextAsset;
              }
              return found->second;
            },
    };

    CollectionPreparation prepared(context);
    const auto* base = prepared.snapshot().asset<InstrumentSetAsset>(baseInstrumentsId);
    expect(base != nullptr, "collection preparation should expose immutable base assets");
    auto instruments = prepared.instruments("effective-instruments");
    for (const auto& value : base->instruments) {
      instruments.add(value.identity->key, value);
    }
    auto copied = instruments.find(3);
    expect(copied.has_value(), "copied instruments should remain addressable by an explicit format key");
    copied->value().name = "Sequence Override";
    auto drumKit = instruments.add(900, Instrument{.name = "Sequence Drum Kit"});
    drumKit.region(SampleRef{.collection = baseSamplesId, .index = 0}, Region{
                                                                           .keyRange =
                                                                               KeyRange{
                                                                                   .low = drumKey,
                                                                                   .high = drumKey,
                                                                               },
                                                                           .range =
                                                                               SourceRange{
                                                                                   .source = source,
                                                                                   .offset = drumKey,
                                                                                   .size = 1,
                                                                               },
                                                                       });
    prepared.replaceInstrumentSet("Effective Instruments", std::move(instruments));

    auto extraSamples = prepared.samples("sequence-samples");
    extraSamples.add(0, Sample{
                            .name = "Sequence Sample",
                            .encodedData = SourceRange{.source = source, .offset = 50, .size = 4},
                        });
    prepared.appendSampleCollection("Sequence Samples", std::move(extraSamples));
    return std::move(prepared).finish();
  };

  const MaterializationResult first = prepare("sequence-a", 40);
  const MaterializationResult second = prepare("sequence-b", 41);
  const MaterializationResult firstAgain = prepare("sequence-a", 40);
  expect(first.assets.size() == 2 && second.assets.size() == 2,
         "collection preparation should collect several independently derived synth assets");
  expect(first.collection.instrumentSets.size() == 1 && first.collection.sampleCollections.size() == 2,
         "replace and append operations should update only their intended collection references");
  expect(first.collection.instrumentSets[0] != second.collection.instrumentSets[0],
         "different sequences should receive different derived instrument-set ids");
  expect(first.collection.instrumentSets[0] == firstAgain.collection.instrumentSets[0],
         "one collection slot should retain a stable derived asset id across rebuilds");

  const auto* firstInstruments = std::get_if<InstrumentSetAsset>(&first.assets[0].asset);
  const auto* secondInstruments = std::get_if<InstrumentSetAsset>(&second.assets[0].asset);
  expect(firstInstruments != nullptr && secondInstruments != nullptr && firstInstruments->instruments.size() == 2 &&
             secondInstruments->instruments.size() == 2,
         "each prepared collection should contain its copied instrument and sequence drum kit");
  expect(firstInstruments->instruments[1].regions[0].keyRange.low == 40 &&
             secondInstruments->instruments[1].regions[0].keyRange.low == 41,
         "sequence-specific recipes should produce independent immutable values");
  expect(first.sourceMap.ownedBy(ObjectRefs::instrument(first.collection.instrumentSets[0], 0)).size() == 1 &&
             first.sourceMap.ownedBy(ObjectRefs::region(first.collection.instrumentSets[0], 1, 0)).size() == 1 &&
             first.sourceMap.ownedBy(ObjectRefs::sample(first.collection.sampleCollections[1], 0)).size() == 1,
         "prepared assets should rebind source annotations to their stable derived object identities");
  const auto* unchangedBase = snapshot.asset<InstrumentSetAsset>(baseInstrumentsId);
  expect(unchangedBase != nullptr && unchangedBase->instruments[0].name == "Base Instrument" &&
             unchangedBase->instruments.size() == 1,
         "collection preparation must never mutate its shared base instrument set");

  DesiredCollection incompleteCollection{
      .key = CollectionKey{.resolver = "Probe", .value = "incomplete"},
      .name = "Incomplete Probe",
  };
  MaterializationContext incompleteContext{
      .sources = sources,
      .snapshot = snapshot,
      .collection = incompleteCollection,
      .ids = ids,
      .assetIdForSlot = [](std::string_view) { return AssetId{200}; },
  };
  CollectionPreparation incomplete(incompleteContext);
  auto abandoned = incomplete.instruments("abandoned-instruments");
  abandoned
      .add(0,
           Instrument{
               .name = "Abandoned Instrument",
               .range = SourceRange{.source = source, .offset = 60, .size = 2},
           })
      .source("Abandoned Instrument", SourceRange{.source = source, .offset = 60, .size = 2});
  const auto incompleteResult = incomplete.incomplete("Base instrument set was not found");
  expect(incompleteResult.collection.status == CollectionStatus::Incomplete &&
             incompleteResult.collection.issues.size() == 1 && incompleteResult.diagnostics.size() == 1,
         "collection preparation should report one coherent issue and diagnostic when binding cannot finish");
  expect(incompleteResult.assets.empty() && incompleteResult.sourceMap.annotations().empty(),
         "an incomplete preparation should discard partial derived assets and their source annotations");
}

void collectionValidationRejectsAmbiguousSynthBindings() {
  const SourceId source{34};
  SessionSnapshotBuilder snapshotBuilder;
  snapshotBuilder.assets.push_back(SampleCollectionAsset{
      .metadata = AssetMetadata{.id = AssetId{80}, .name = "Samples A"},
      .samples = SampleCollection{.samples = {Sample{.name = "A"}}},
  });
  snapshotBuilder.assets.push_back(SampleCollectionAsset{
      .metadata = AssetMetadata{.id = AssetId{81}, .name = "Samples B"},
      .samples = SampleCollection{.samples = {Sample{.name = "B"}}},
  });
  snapshotBuilder.assets.push_back(InstrumentSetAsset{
      .metadata = AssetMetadata{.id = AssetId{82}, .name = "Instruments A"},
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 5},
          .identity = InstrumentIdentity{.domain = "probe", .key = 1},
          .regions =
              {
                  Region{
                      .sample = SampleRef{.collection = AssetId{99}, .index = 0},
                      .range = SourceRange{.source = source, .offset = 10, .size = 1},
                  },
                  Region{
                      .sample = SampleRef{.collection = AssetId{80}, .index = 9},
                      .range = SourceRange{.source = source, .offset = 11, .size = 1},
                  },
                  Region{
                      .sample = SampleRef{.index = 0},
                      .range = SourceRange{.source = source, .offset = 12, .size = 1},
                  },
              },
      }},
  });
  snapshotBuilder.assets.push_back(InstrumentSetAsset{
      .metadata = AssetMetadata{.id = AssetId{83}, .name = "Instruments B"},
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 5},
          .identity = InstrumentIdentity{.domain = "probe", .key = 1},
      }},
  });
  snapshotBuilder.collections.push_back(Collection{
      .id = CollectionId{84},
      .name = "Ambiguous Synth",
      .instrumentSets = {AssetId{82}, AssetId{83}},
      .sampleCollections = {AssetId{80}, AssetId{81}},
  });
  const SessionSnapshot snapshot = snapshotBuilder.finish();
  const ValidationReport report = validateSessionSnapshot(snapshot);
  const auto hasCode = [&](std::string_view code) {
    return std::ranges::any_of(report.findings(), [=](const ValidationFinding& finding) {
      return finding.code == code && finding.collection == CollectionId{84};
    });
  };
  expect(hasCode("snapshot.collection.duplicate-instrument-identity"),
         "collection validation should reject cross-set identity ambiguity");
  expect(hasCode("snapshot.collection.conflicting-instrument-address"),
         "collection validation should report cross-set explicit address conflicts");
  expect(hasCode("snapshot.collection.region-sample-collection-missing"),
         "collection validation should reject region references to unattached sample sets");
  expect(hasCode("snapshot.collection.region-sample-index-invalid"),
         "collection validation should reject out-of-range concrete sample indexes");
  expect(hasCode("snapshot.collection.region-sample-collection-ambiguous"),
         "collection validation should reject implicit first-match behavior with several sample sets");

  SessionSnapshotBuilder validBuilder;
  validBuilder.assets = {snapshot.assets()[0], InstrumentSetAsset{
                                                   .metadata = AssetMetadata{.id = AssetId{85}},
                                                   .instruments = {Instrument{
                                                       .regions = {Region{.sample = SampleRef{.index = 0}}},
                                                   }},
                                               }};
  validBuilder.collections.push_back(Collection{
      .id = CollectionId{86},
      .instrumentSets = {AssetId{85}},
      .sampleCollections = {AssetId{80}},
  });
  expect(validateSessionSnapshot(validBuilder.finish()).empty(),
         "one attached sample collection should remain a valid implicit reference for legacy formats");
}

}  // namespace

void runValueSynthBuilderTests() {
  recordReaderFinishesOnePortableSourceValue();
  sampleBuilderKeepsKeysDenseAndAnnotationsOwned();
  instrumentBuilderGroupsEntriesAndProjectsRegionIdentity();
  scanResultBuilderCommitsSynthBuildersExplicitly();
  scanResultBuilderRetainsSampleKeysAndExposesExistingRegions();
  valueEscapeHatchContributesFinalRanges();
  detachedBuildersUseTheSameAuthoringSurface();
  collectionPreparationBuildsImmutableSequenceSpecificAssets();
  collectionValidationRejectsAmbiguousSynthBindings();
}

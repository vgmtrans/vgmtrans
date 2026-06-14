/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

namespace {

void projectSessionScansValuesAndVirtualSources() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.formats().add(probeMiscModule());
  session.dialects().add(probeSequenceDialect());

  const auto sourceId = session.addSource(SourceFile{.name = "probe.spc"}, {0xaa, 0x34, 0x12});
  expect(sourceId == SourceId{0}, "first source should get SourceId 0");

  Project project = session.scan();
  expect(project.sources.size() == 2, "scan should include extracted virtual source");
  expect(project.sources[1].virtualized, "extracted source should be virtualized");
  expect(project.sources[1].origin.has_value() && project.sources[1].origin->source == sourceId &&
             project.sources[1].origin->offset == 0 && project.sources[1].origin->size == 1,
         "extracted virtual source should preserve its origin range");
  expect(project.assets.size() == 2, "scan should produce sequence and misc assets");
  expect(project.collections.size() == 1, "scan should produce one collection");
  expect(project.diagnostics.size() == 1, "scan should preserve module diagnostics");
  expect(project.index.valid && project.index.assetsById.size() == 2 && project.index.collectionsById.size() == 1,
         "scan should publish an indexed project snapshot");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets[0]);
  expect(sequence != nullptr, "first asset should be a sequence");
  expect(sequence->metadata.id == AssetId{0}, "sequence should keep allocated asset id");
  expect(assetById(project, sequence->metadata.id) == &project.assets[0],
         "project snapshot should find an asset by stable id");
  expect(assetById<SequenceProgramAsset>(project, sequence->metadata.id) == sequence,
         "project snapshot should find a sequence program asset by stable id");
  expect(assetById<MiscAsset>(project, sequence->metadata.id) == nullptr,
         "project snapshot should reject asset id lookups with the wrong value type");
  expect(assetById(project, AssetId{99}) == nullptr, "project snapshot should return null for a missing asset id");
  expect(assetById<SequenceProgramAsset>(project, AssetId{99}) == nullptr,
         "project snapshot should return null for a missing asset id");
  expect(sequence->metadata.items.nodes.size() == 2, "sequence should expose item tree");
  expect(sequence->metadata.items.root == sequence->metadata.items.nodes[0].id,
         "scanner should preserve valid item tree root");
  expect(sequence->metadata.items.nodes[0].children == std::vector<ItemId>{sequence->metadata.items.nodes[1].id},
         "scanner should rebuild item children from parent links");
  expect(itemById(sequence->metadata.items, sequence->metadata.items.nodes[0].id) == &sequence->metadata.items.nodes[0],
         "item tree should find its root item by stable id");
  expect(itemById(sequence->metadata.items, sequence->metadata.items.nodes[1].id) == &sequence->metadata.items.nodes[1],
         "item tree should find child items by stable id");
  expect(itemById(sequence->metadata.items, ItemId{99}) == nullptr,
         "item tree should return null for a missing item id");
  expect(project.collections[0].sequence == sequence->metadata.id, "collection should reference sequence asset");
  expect(collectionById(project, project.collections[0].id) == &project.collections[0],
         "project snapshot should find a collection by stable id");
  expect(collectionById(project, CollectionId{99}) == nullptr,
         "project snapshot should return null for a missing collection id");

  const auto* misc = std::get_if<MiscAsset>(&project.assets[1]);
  expect(misc != nullptr, "second asset should be misc from virtual source");
  expect(metadata(project.assets[1]).id == AssetId{1}, "missing asset id should be assigned");

  project = session.scan();
  expect(project.sources.size() == 2, "rescan should replace, not duplicate, virtual tail sources");
}

void projectSessionReportsUnregisteredSequenceDialect() {
  Session session;
  session.formats().add(probeSequenceModule());

  session.addSource(SourceFile{.name = "missing-dialect.probe"}, {0xaa});
  const Project project = session.scan();
  expect(project.collections.size() == 1, "missing dialect fixture should still scan sequence collections");
  expect(project.diagnostics.size() == 2, "missing dialect fixture should keep scan and registration diagnostics");

  const auto& diagnostic = diagnosticWithMessage(project.diagnostics, "No sequence dialect registered for 'probe'");
  expect(diagnostic.severity == Severity::Error, "missing sequence dialect should be reported as an error");
  expect(diagnostic.range && diagnostic.range->source == SourceId{0} && diagnostic.range->offset == 0 &&
             diagnostic.range->size == 1,
         "missing sequence dialect diagnostic should point at the sequence asset range");

  const auto exports = session.exportAllCollections(ExportRequest{
      .kinds = {ExportKind::Midi},
  });
  expect(exports.size() == 1, "missing dialect fixture should still attempt collection export");
  expect(exports[0].artifacts.size() == 1, "missing dialect fixture should still return one MIDI artifact");
  expectDiagnosticRange(exports[0].artifacts[0].diagnostics, "No sequence dialect registered for 'probe'",
                        SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void projectFinalizationReportsDuplicateIds() {
  Project project;
  project.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{7},
              .format = "Probe",
              .name = "First",
              .range = probeRange(4, 1),
          },
  });
  project.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{7},
              .format = "Probe",
              .name = "Duplicate",
              .range = probeRange(8, 1),
          },
  });
  project.collections.push_back(Collection{
      .id = CollectionId{3},
      .name = "First",
      .miscAssets = {AssetId{7}},
  });
  project.collections.push_back(Collection{
      .id = CollectionId{3},
      .name = "Duplicate",
      .miscAssets = {AssetId{7}},
  });

  finalizeProjectIndex(project);

  expect(project.index.valid, "project finalization should publish an index");
  expect(assetById(project, AssetId{7}) == &project.assets[0],
         "duplicate asset id lookup should keep the first asset");
  expect(collectionById(project, CollectionId{3}) == &project.collections[0],
         "duplicate collection id lookup should keep the first collection");
  expect(project.diagnostics.size() == 2, "project finalization should report duplicate ids");

  const auto& assetDiagnostic = diagnosticWithMessage(project.diagnostics, "Duplicate asset id 7 in Project snapshot");
  expect(assetDiagnostic.severity == Severity::Error, "duplicate asset id should be reported as an error");
  expect(assetDiagnostic.range && sameRange(*assetDiagnostic.range, probeRange(8, 1)),
         "duplicate asset id diagnostic should point at the conflicting asset");

  const auto& collectionDiagnostic =
      diagnosticWithMessage(project.diagnostics, "Duplicate collection id 3 in Project snapshot");
  expect(collectionDiagnostic.severity == Severity::Error, "duplicate collection id should be reported as an error");
  expect(!collectionDiagnostic.range.has_value(), "collection id diagnostics should not invent a source range");
}

void projectCollectionAssetResolutionProvidesTypedExportInputs() {
  Project project;
  project.assets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{0},
              .format = "Probe",
              .name = "Sequence",
          },
      .program =
          SequenceProgram{
              .dialect = DialectId{.value = "probe"},
              .timebase = Timebase{.ppqn = 48},
          },
  });
  project.assets.emplace_back(InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Instruments",
          },
  });
  project.assets.emplace_back(SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Samples",
          },
  });
  project.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{3},
              .format = "Probe",
              .name = "Misc",
          },
  });
  project.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Full",
      .sequence = AssetId{0},
      .instrumentSets = {AssetId{1}, AssetId{41}},
      .sampleCollections = {AssetId{2}, AssetId{42}},
      .miscAssets = {AssetId{3}, AssetId{43}},
  });
  project.collections.push_back(Collection{
      .id = CollectionId{1},
      .name = "Samples Only",
      .sampleCollections = {AssetId{2}},
  });

  const auto full = resolveCollectionAssets(project, CollectionId{0});
  expect(full.collection == &project.collections[0], "collection asset resolver should preserve the collection");
  expect(full.sequenceProgram == std::get_if<SequenceProgramAsset>(&project.assets[0]),
         "collection asset resolver should resolve the typed sequence program asset");
  expect(
      full.instrumentSets.size() == 1 && full.instrumentSets[0] == std::get_if<InstrumentSetAsset>(&project.assets[1]),
      "collection asset resolver should resolve typed instrument set assets");
  expect(full.sampleCollections.size() == 1 &&
             full.sampleCollections[0] == std::get_if<SampleCollectionAsset>(&project.assets[2]),
         "collection asset resolver should resolve typed sample collection assets");
  expect(full.miscAssets.size() == 1 && full.miscAssets[0] == std::get_if<MiscAsset>(&project.assets[3]),
         "collection asset resolver should resolve typed misc assets");
  expect(full.diagnostics.sequence.empty(), "valid sequence references should not produce diagnostics");
  expect(full.diagnostics.instrumentSets.size() == 1,
         "collection asset resolver should report broken instrument references separately");
  expect(full.diagnostics.sampleCollections.size() == 1,
         "collection asset resolver should report broken sample references separately");
  expect(full.diagnostics.miscAssets.size() == 1,
         "collection asset resolver should report broken misc references separately");
  expect(full.diagnostics.all().size() == 3, "collection asset resolver should aggregate reference diagnostics");

  const auto samplesOnly = resolveCollectionAssets(project, CollectionId{1});
  expect(samplesOnly.collection == &project.collections[1],
         "collection asset resolver should resolve sample-only collections");
  expect(samplesOnly.sequenceProgram == nullptr, "sample-only collections should not report a sequence asset");
  expect(samplesOnly.diagnostics.sequence.empty(),
         "absent optional sequence references should not be treated as broken references");
  expect(samplesOnly.sampleCollections.size() == 1,
         "sample-only collections should still resolve their sample collections");

  const auto missing = resolveCollectionAssets(project, CollectionId{99});
  expect(missing.collection == nullptr, "missing collection resolver result should not expose a collection");
  expect(missing.diagnostics.collection.size() == 1,
         "missing collection resolver result should report a collection diagnostic");
}

void projectSessionAddsSourceFromPath() {
  const auto path = std::filesystem::temp_directory_path() / "vgmtrans-value-core-source-load.bin";
  std::filesystem::remove(path);
  {
    std::ofstream out(path, std::ios::binary);
    out.put(static_cast<char>(0xaa));
    out.put(static_cast<char>(0x34));
    out.put(static_cast<char>(0x12));
  }

  Session session;
  session.formats().add(probeSequenceModule());
  session.dialects().add(probeSequenceDialect());

  const auto sourceId = session.addSourceFromPath(path);
  expect(sourceId == SourceId{0}, "path source should get SourceId 0");
  expect(session.sources().source(sourceId).name == path.filename().string(),
         "path source should use the filename as source name");
  expect(session.sources().source(sourceId).path == path, "path source should preserve filesystem path");
  const std::array<u8, 3> expectedBytes{0xaa, 0x34, 0x12};
  expect(std::ranges::equal(session.sources().bytes(sourceId), expectedBytes),
         "path source should preserve file bytes");

  const Project project = session.scan();
  expect(project.collections.size() == 1, "path source should scan through registered modules");
  expect(project.sources.front().path == path, "project snapshot should preserve path source metadata");

  std::filesystem::remove(path);
}

void projectSessionExportsAllCollections() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.dialects().add(probeSequenceDialect());

  session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  const Project project = session.scan();
  expect(project.collections.size() == 2, "probe sources should produce two collections");

  const auto exports = session.exportAllCollections(ExportRequest{
      .kinds = {ExportKind::Midi},
  });
  expect(exports.size() == project.collections.size(), "all-collection export should cover every collection");

  for (size_t i = 0; i < exports.size(); ++i) {
    expect(exports[i].collection == project.collections[i].id,
           "all-collection export should preserve collection ids in project order");
    expect(exports[i].artifacts.size() == 1, "probe MIDI export should return one artifact per collection");
    expect(exports[i].artifacts[0].filename == project.collections[i].name + ".mid",
           "collection export should keep collection-derived artifact names");
    expect(exports[i].artifacts[0].mediaType == "audio/midi", "collection export should keep artifact media types");
    expect(exports[i].artifacts[0].diagnostics.empty(),
           "registered probe sequence exports should not report missing dialect diagnostics");
  }
}


}  // namespace

void runValueSessionTests() {
  projectSessionScansValuesAndVirtualSources();
  projectSessionReportsUnregisteredSequenceDialect();
  projectFinalizationReportsDuplicateIds();
  projectCollectionAssetResolutionProvidesTypedExportInputs();
  projectSessionAddsSourceFromPath();
  projectSessionExportsAllCollections();
}

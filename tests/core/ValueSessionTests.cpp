/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/scan/CollectionPreparation.h"
#include "value/session/ScanCommit.h"
#include "value/validation/ScanValidation.h"
#include "value/validation/SnapshotValidation.h"

namespace {

void sessionScansValuesAndDerivedSources() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.formats().add(probeMiscModule());
  session.dialects().add(probeSequenceDialect());

  const auto sourceId = session.addSource(SourceFile{.name = "probe.spc"}, {0xaa, 0x34, 0x12});
  expect(sourceId == SourceId{0}, "first source should get SourceId 0");

  SessionSnapshot snapshot = session.scanPendingSources();
  expect(snapshot.sources().size() == 2, "scan should include extracted derived source");
  expect(snapshot.sources()[1].derived(), "extracted source should be derived");
  expect(snapshot.sources()[1].origin.has_value() && snapshot.sources()[1].origin->source == sourceId &&
             snapshot.sources()[1].origin->offset == 0 && snapshot.sources()[1].origin->size == 1,
         "extracted derived source should preserve its origin range");
  expect(snapshot.assets().size() == 2, "scan should produce sequence and misc assets");
  expect(snapshot.collections().size() == 1, "scan should produce one collection");
  expect(snapshot.diagnostics().size() == 1, "scan should preserve module diagnostics");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&snapshot.assets()[0]);
  expect(sequence != nullptr, "first asset should be a sequence");
  expect(sequence->metadata.id == AssetId{0}, "sequence should keep allocated asset id");
  expect(assetById(snapshot, sequence->metadata.id) == &snapshot.assets()[0],
         "session snapshot should find an asset by stable id");
  expect(assetById<SequenceProgramAsset>(snapshot, sequence->metadata.id) == sequence,
         "session snapshot should find a sequence program asset by stable id");
  expect(assetById<MiscAsset>(snapshot, sequence->metadata.id) == nullptr,
         "session snapshot should reject asset id lookups with the wrong value type");
  expect(assetById(snapshot, AssetId{99}) == nullptr, "session snapshot should return null for a missing asset id");
  expect(assetById<SequenceProgramAsset>(snapshot, AssetId{99}) == nullptr,
         "session snapshot should return null for a missing asset id");
  const SourceMap& sourceMap = snapshot.sourceMap();
  const auto sequenceAnnotations = sourceMap.withRole(sourceId, SourceRole::Sequence);
  expect(sequenceAnnotations.size() == 1, "sequence should expose a source-root annotation");
  const SourceAnnotation& sequenceRoot = sourceMap.get(sequenceAnnotations.front());
  expect(sequenceRoot.owner == ObjectRefs::sequence(sequence->metadata.id),
         "sequence root annotation should point at the semantic sequence asset");
  const auto rootChildren = sourceMap.childrenOf(sequenceRoot.id);
  expect(rootChildren.size() == 1, "source annotations should preserve parent-child relationships");
  expect(sourceMap.get(rootChildren.front()).role == SourceRole::Header,
         "source annotation children should support tree view nodes");
  expect(sourceMap.find(SourceAnnotationId{99}) == nullptr,
         "source map should return null for a missing annotation id");
  expect(snapshot.collections()[0].sequence == sequence->metadata.id, "collection should reference sequence asset");
  expect(collectionById(snapshot, snapshot.collections()[0].id) == &snapshot.collections()[0],
         "session snapshot should find a collection by stable id");
  expect(collectionById(snapshot, CollectionId{99}) == nullptr,
         "session snapshot should return null for a missing collection id");

  const auto* misc = std::get_if<MiscAsset>(&snapshot.assets()[1]);
  expect(misc != nullptr, "second asset should be misc from derived source");
  expect(metadata(snapshot.assets()[1]).id == AssetId{1}, "missing asset id should be assigned");

  snapshot = session.scanPendingSources();
  expect(snapshot.sources().size() == 2, "pending-source scan should not duplicate already-scanned derived sources");
  expect(snapshot.assets().size() == 2, "pending-source scan should not duplicate already-scanned assets");
  expect(snapshot.collections().size() == 1, "pending-source scan should not duplicate already-resolved collections");
}

void sessionReportsUnregisteredSequenceDialect() {
  Session session;
  session.formats().add(probeSequenceModule());

  session.addSource(SourceFile{.name = "missing-dialect.probe"}, {0xaa});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.collections().size() == 1, "missing dialect fixture should still scan sequence collections");
  expect(project.diagnostics().size() == 2, "missing dialect fixture should keep scan and registration diagnostics");

  const auto& diagnostic = diagnosticWithMessage(project.diagnostics(), "No sequence dialect registered for 'probe'");
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

void sessionScansIndividualSourcesWithoutDuplicating() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.dialects().add(probeSequenceDialect());

  const auto first = session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  SessionSnapshot project = session.scanSource(first);
  expect(project.assets().size() == 1, "source scan should add assets from the requested source");
  expect(project.collections().size() == 1, "source scan should resolve collections after the scan transaction");

  project = session.scanSource(first);
  expect(project.assets().size() == 1, "repeat source scan should not duplicate already-scanned assets");
  expect(project.collections().size() == 1, "repeat source scan should not duplicate already-resolved collections");

  const auto second = session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  project = session.scanSource(second);
  expect(project.assets().size() == 2, "later source scan should preserve previous assets and add the new source");
  expect(project.collections().size() == 2,
         "later source scan should preserve previous collections and add the new one");

  project = session.scanPendingSources();
  expect(project.assets().size() == 2, "pending-source scan should skip already-scanned user sources");
  expect(project.collections().size() == 2, "pending-source scan should leave existing collections unchanged");
}

void sessionKeepsScannerKnownCollectionsWithoutResolver() {
  Session session;
  session.formats().add(probeExplicitCollectionModule());
  session.dialects().add(probeSequenceDialect());

  const auto source = session.addSource(SourceFile{.name = "explicit.probe"}, {0xab});
  SessionSnapshot project = session.scanSource(source);
  expect(project.matchFacts().empty(), "explicit scanner-known collection should not need match facts");
  expect(project.collections().size() == 1, "explicit scanner-known collection should be published");
  expect(project.collections()[0].key.resolver == "ProbeExplicit",
         "explicit scanner-known collection should use its scanner resolver key");

  project = session.removeSource(source);
  expect(project.collections().empty(), "explicit scanner-known collection should disappear with its source");
}

void sessionMatchesCollectionsAcrossSeparateSourceScans() {
  Session session;
  session.formats().add(probeBankSequenceModule());
  session.formats().add(probeBankInstrumentModule());
  session.dialects().add(probeSequenceDialect());

  const auto instrument = session.addSource(SourceFile{.name = "bank-7.instr"}, {0xdd, 7});
  SessionSnapshot project = session.scanSource(instrument);
  expect(project.assets().size() == 1, "instrument scan should add its asset immediately");
  expect(project.collections().size() == 1, "resolver should keep an incomplete collection for a partial match");
  expect(project.collections()[0].status == CollectionStatus::Incomplete,
         "instrument-only bank collection should be marked incomplete");
  expect(project.collections()[0].instrumentSets.size() == 1,
         "instrument-only bank collection should reference the instrument set");
  const CollectionId bankCollection = project.collections()[0].id;

  const auto sequence = session.addSource(SourceFile{.name = "bank-7.seq"}, {0xcc, 7});
  project = session.scanSource(sequence);
  expect(project.assets().size() == 2, "second source scan should add the matching sequence asset");
  expect(project.collections().size() == 1, "matching facts should update the existing bank collection");
  expect(project.collections()[0].id == bankCollection, "resolver update should preserve the collection id");
  expect(project.collections()[0].status == CollectionStatus::Complete,
         "bank collection should become complete when sequence and instruments are both present");
  expect(project.collections()[0].sequence.has_value(), "completed bank collection should reference the sequence");
  expect(project.collections()[0].instrumentSets.size() == 1,
         "completed bank collection should retain the instrument reference");
}

[[nodiscard]] MaterializationResult materializeProbeBankCollection(const MaterializationContext& context) {
  CollectionPreparation prepared(context);
  if (!context.collection.sequence || context.collection.instrumentSets.empty()) {
    return std::move(prepared).finish();
  }

  const auto* sequence = context.snapshot.asset<SequenceProgramAsset>(*context.collection.sequence);
  const auto* scannedInstrument = context.snapshot.asset<InstrumentSetAsset>(context.collection.instrumentSets[0]);
  if (sequence == nullptr || scannedInstrument == nullptr) {
    return std::move(prepared).finish();
  }

  auto instruments = prepared.instruments("bound-instrument-set");
  instruments.include(sequence->metadata.range);
  for (const auto& instrument : scannedInstrument->instruments) {
    instruments.append(instrument);
  }
  instruments.append(Instrument{
      .name = "Materialized Instrument",
      .range = sequence->metadata.range,
  });
  prepared.replaceInstrumentSet("Materialized " + scannedInstrument->metadata.name, std::move(instruments));
  return std::move(prepared).finish();
}

[[nodiscard]] FormatModule probeMaterializedBankSequenceModule() {
  auto module = probeBankSequenceModule();
  module.materializeCollection = materializeProbeBankCollection;
  return module;
}

void sessionMaterializesResolvedCollectionsWithStableAssets() {
  Session session;
  session.formats().add(probeMaterializedBankSequenceModule());
  session.formats().add(probeBankInstrumentModule());
  session.dialects().add(probeSequenceDialect());

  const auto instrument = session.addSource(SourceFile{.name = "bank-11.instr"}, {0xdd, 11});
  session.addSource(SourceFile{.name = "bank-11.seq"}, {0xcc, 11});
  SessionSnapshot project = session.scanPendingSources();
  expect(project.collections().size() == 1, "materialized bank files should produce one collection");
  expect(project.assets().size() == 3, "materialization should add a derived collection asset");

  const auto& collection = project.collections()[0];
  expect(collection.instrumentSets.size() == 1, "materialized collection should expose one instrument set");
  expect(collection.instrumentSets[0] != AssetId{0} && collection.instrumentSets[0] != AssetId{1},
         "materialized collection should not expose either scanned input asset as its final instrument set");
  const AssetId materializedId = collection.instrumentSets[0];
  const auto* materialized = project.asset<InstrumentSetAsset>(materializedId);
  expect(materialized != nullptr, "materialized instrument set should be present in the snapshot");
  expect(materialized->metadata.name == "Materialized bank-11.instr",
         "materializer should control the derived asset contents");
  expect(materialized->instruments.size() == 1, "materialized instrument set should keep derived instrument data");
  expect(project.sourceMap().ownedBy(ObjectRefs::instrument(materializedId, 0)).size() == 1,
         "materialized builder should publish annotations owned by the derived instrument");

  project = session.scanPendingSources();
  expect(project.collections()[0].instrumentSets[0] == materializedId,
         "materialized asset id should be stable across collection rebuilds");
  expect(project.sourceMap().ownedBy(ObjectRefs::instrument(materializedId, 0)).size() == 1,
         "rebuilding one stable materialization slot should replace rather than duplicate its annotations");

  project = session.removeSource(instrument);
  expect(project.collections().size() == 1, "removing one matched source should leave an incomplete collection");
  expect(project.collections()[0].instrumentSets.empty(),
         "collection should fall back to no instrument set when materialization input disappears");
  expect(project.asset(materializedId) == nullptr, "stale materialized asset should be removed with its collection");
  expect(project.sourceMap().ownedBy(ObjectRefs::instrument(materializedId, 0)).empty(),
         "removing a stale materialized asset should also remove its owned annotations");
}

void sessionRemovesSourceFamilyAndDiscoveredData() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.formats().add(probeMiscModule());
  session.dialects().add(probeSequenceDialect());

  const auto source = session.addSource(SourceFile{.name = "remove-me.probe"}, {0xaa, 0x34});
  SessionSnapshot project = session.scanSource(source);
  expect(project.sources().size() == 2, "fixture should scan one user source and one derived source");
  expect(project.assets().size() == 2, "fixture should scan user and derived assets");
  expect(project.matchFacts().size() == 1, "fixture should publish a collection match fact");
  expect(project.collections().size() == 1, "fixture should publish one collection");
  expect(project.diagnostics().size() == 1, "fixture should publish one source-backed diagnostic");

  project = session.removeSource(source);
  expect(project.sources().empty(), "removed source family should disappear from snapshots");
  expect(project.assets().empty(), "removed source family should remove discovered assets");
  expect(project.matchFacts().empty(), "removed source family should remove match facts");
  expect(project.collections().empty(), "removed source family should remove discovered collections");
  expect(project.diagnostics().empty(), "removed source family should remove source-backed diagnostics");
  expect(session.sources().sourceCount() == 0, "source store should count only active sources");
  expect(!session.sources().contains(source), "removed source should no longer be readable");

  bool readRemovedSourceFailed = false;
  try {
    static_cast<void>(session.sources().bytes(source));
  } catch (const std::out_of_range&) {
    readRemovedSourceFailed = true;
  }
  expect(readRemovedSourceFailed, "removed source bytes should be inaccessible");

  bool scanRemovedSourceFailed = false;
  try {
    static_cast<void>(session.scanSource(source));
  } catch (const std::out_of_range&) {
    scanRemovedSourceFailed = true;
  }
  expect(scanRemovedSourceFailed, "removed sources should not be scannable");

  const auto replacement = session.addSource(SourceFile{.name = "replacement.probe"}, {0xaa});
  expect(replacement == SourceId{2}, "source ids should not be reused after removing a source family");
  project = session.scanPendingSources();
  expect(project.sources().size() == 2, "replacement scan should add a new derived source");
  expect(project.sources()[0].id == replacement, "replacement user source should keep its new stable id");
}

void sessionRemovalUpdatesCrossSourceCollectionLifecycle() {
  Session session;
  session.formats().add(probeBankSequenceModule());
  session.formats().add(probeBankInstrumentModule());
  session.dialects().add(probeSequenceDialect());

  const auto instrument = session.addSource(SourceFile{.name = "bank-9.instr"}, {0xdd, 9});
  const auto sequence = session.addSource(SourceFile{.name = "bank-9.seq"}, {0xcc, 9});
  SessionSnapshot project = session.scanPendingSources();
  expect(project.collections().size() == 1, "matching bank files should produce one collection");
  expect(project.collections()[0].status == CollectionStatus::Complete, "matched bank collection should be complete");
  const CollectionId collectionId = project.collections()[0].id;

  project = session.removeSource(instrument);
  expect(project.sources().size() == 1, "removing one matched source should leave the other source active");
  expect(project.assets().size() == 1, "removing one matched source should leave the other asset active");
  expect(project.matchFacts().size() == 1, "removing one matched source should leave the other match fact active");
  expect(project.collections().size() == 1, "remaining match fact should keep the bank collection alive");
  expect(project.collections()[0].id == collectionId, "collection id should be preserved for the same key");
  expect(project.collections()[0].status == CollectionStatus::Incomplete,
         "remaining sequence-only collection should become incomplete");
  expect(project.collections()[0].sequence.has_value(), "remaining collection should keep the sequence asset");
  expect(project.collections()[0].instrumentSets.empty(),
         "removed instrument source should be removed from the collection");
  expect(!project.collections()[0].issues.empty(), "incomplete collection should explain what is missing");

  project = session.removeSource(sequence);
  expect(project.sources().empty(), "removing the last matched source should leave no active sources");
  expect(project.assets().empty(), "removing the last matched source should leave no assets");
  expect(project.matchFacts().empty(), "removing the last matched source should leave no match facts");
  expect(project.collections().empty(), "resolver-owned discovered collection should disappear when no facts remain");
}

void sessionResolverFailureDoesNotWipeExistingCollections() {
  Session session;
  session.formats().add(fragileProbeSequenceModule());
  session.dialects().add(probeSequenceDialect());

  const auto first = session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  SessionSnapshot project = session.scanSource(first);
  expect(project.collections().size() == 1, "initial scan should create a collection");
  const CollectionId originalCollection = project.collections()[0].id;

  session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  project = session.scanPendingSources();
  expect(project.collections().size() == 1, "resolver failure should preserve previous collections");
  expect(project.collections()[0].id == originalCollection, "preserved collection should keep its id");
  static_cast<void>(diagnosticWithMessage(project.diagnostics(),
                                          "ProbeSequenceFragileResolver resolveCollections failed: resolver exploded"));
}

void sessionMarksCollectionsStaleWhenRemovalCannotReconcile() {
  Session session;
  session.formats().add(fragileProbeSequenceModule());
  session.dialects().add(probeSequenceDialect());

  const auto source = session.addSource(SourceFile{.name = "stale-on-failure.probe"}, {0xaa});
  SessionSnapshot project = session.scanSource(source);
  expect(project.collections().size() == 1, "initial scan should create a collection");
  const CollectionId originalCollection = project.collections()[0].id;

  project = session.removeSource(source);
  expect(project.sources().empty(), "failed reconcile after removal should still remove sources");
  expect(project.assets().empty(), "failed reconcile after removal should still remove assets");
  expect(project.matchFacts().empty(), "failed reconcile after removal should still remove match facts");
  expect(project.collections().size() == 1, "resolver failure should keep the previous collection inspectable");
  expect(project.collections()[0].id == originalCollection, "stale collection should keep its id");
  expect(project.collections()[0].status == CollectionStatus::Stale,
         "collection should be marked stale when cleanup cannot reconcile its resolver");
  expect(!project.collections()[0].issues.empty(), "stale collection should explain why it is stale");
  static_cast<void>(diagnosticWithMessage(project.diagnostics(),
                                          "ProbeSequenceFragileResolver resolveCollections failed: resolver exploded"));
}

void sessionRejectsLateRegistryMutation() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.dialects().add(probeSequenceDialect());

  session.addSource(SourceFile{.name = "sealed.probe"}, {0xaa});

  bool formatFailed = false;
  try {
    session.formats().add(probeMiscModule());
  } catch (const std::logic_error&) {
    formatFailed = true;
  }
  expect(formatFailed, "format registry should be sealed after session mutation starts");

  bool dialectFailed = false;
  try {
    session.dialects().add(probeSequenceDialect());
  } catch (const std::logic_error&) {
    dialectFailed = true;
  }
  expect(dialectFailed, "sequence dialect registry should be sealed after session mutation starts");

  Session scannedEmptySession;
  static_cast<void>(scannedEmptySession.scanPendingSources());

  bool emptyScanSealed = false;
  try {
    scannedEmptySession.formats().add(probeSequenceModule());
  } catch (const std::logic_error&) {
    emptyScanSealed = true;
  }
  expect(emptyScanSealed, "format registry should also be sealed by an explicit scan");
}

void sessionRejectsDuplicateAssetIdsAtScanCommit() {
  Session session;
  session.formats().add(probeDuplicateAssetModule());

  session.addSource(SourceFile{.name = "duplicate.probe"}, {0xee});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.assets().empty(), "duplicate asset ids should reject the whole scan result before commit");
  expect(project.collections().empty(), "rejected duplicate asset scan should not create collections");
  expectDiagnosticRange(project.diagnostics(), "ProbeDuplicate scan failed: Scan result contained duplicate asset id 7",
                        SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void sessionRejectsExtractedSourcesWithMissingParents() {
  Session session;
  session.formats().add(probeBadExtractedSourceModule());
  session.formats().add(probeMiscModule());

  session.addSource(SourceFile{.name = "bad-derived-parent.probe"}, {0xf1});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.sources().size() == 1, "bad extracted source should not be added to the session");
  expect(project.assets().empty(), "bad extracted source should reject staged scan assets before commit");
  expectDiagnosticRange(
      project.diagnostics(),
      "ProbeBadExtracted scan failed: Scan result contained extracted source with missing parent source 99",
      SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void sessionRejectsMatchFactsForMissingAssets() {
  Session session;
  session.formats().add(probeBadFactAssetModule());

  session.addSource(SourceFile{.name = "bad-fact-asset.probe"}, {0xf2});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.assets().empty(), "invalid match fact should reject the whole scan result before commit");
  expect(project.matchFacts().empty(), "invalid match fact should not be committed");
  expectDiagnosticRange(project.diagnostics(),
                        "ProbeBadFactAsset scan failed: Scan result contained a match fact for missing asset id 99",
                        SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void sessionRejectsSourceScopedMatchFactsForMissingSources() {
  Session session;
  session.formats().add(probeBadFactSourceModule());

  session.addSource(SourceFile{.name = "bad-fact-source.probe"}, {0xf3});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.assets().empty(), "source-scoped invalid fact should reject the whole scan result before commit");
  expect(project.matchFacts().empty(), "source-scoped invalid fact should not be committed");
  expectDiagnosticRange(project.diagnostics(),
                        "ProbeBadFactSource scan failed: Scan result contained a match fact for missing source id 99",
                        SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void scanValidationReportsMultipleAdmissionErrors() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "multi-error.probe"}, {0xaa});
  const auto goodRange = sources.reader(source).range(0, 1);

  ScanCommit commit{
      .source = source,
      .sourceSize = 1,
      .assets =
          {
              MiscAsset{.metadata =
                            AssetMetadata{
                                .id = AssetId{7},
                                .format = "ProbeValidation",
                                .name = "First",
                                .range = goodRange,
                            }},
              MiscAsset{.metadata =
                            AssetMetadata{
                                .id = AssetId{7},
                                .format = "ProbeValidation",
                                .name = "Duplicate",
                                .range = goodRange,
                            }},
          },
      .matchFacts =
          {
              MatchFact{
                  .asset = AssetId{99},
                  .format = "ProbeValidation",
                  .scope = MatchScope{.kind = MatchScopeKind::Source, .source = SourceId{99}},
                  .payload = IdMatchFact{.domain = "probe", .value = 1},
              },
          },
  };

  AssetStore existingAssets;
  const auto report = validateScanCommit(commit, sources, existingAssets);
  expect(report.hasErrors(), "scan validation should report admission errors");

  bool sawDuplicateAsset = false;
  bool sawMissingFactAsset = false;
  bool sawMissingFactSource = false;
  for (const auto& finding : report.findings()) {
    sawDuplicateAsset = sawDuplicateAsset || finding.message == "Scan result contained duplicate asset id 7";
    sawMissingFactAsset =
        sawMissingFactAsset || finding.message == "Scan result contained a match fact for missing asset id 99";
    sawMissingFactSource =
        sawMissingFactSource || finding.message == "Scan result contained a match fact for missing source id 99";
  }
  expect(sawDuplicateAsset, "scan validation should report duplicate asset ids");
  expect(sawMissingFactAsset, "scan validation should report match facts for missing assets");
  expect(sawMissingFactSource, "scan validation should report match facts for missing sources");
}

[[nodiscard]] AssetMetadata badRangeMetadata(AssetId id, std::string name, SourceRange range) {
  return AssetMetadata{
      .id = id,
      .format = "ProbeBadRange",
      .name = std::move(name),
      .range = range,
  };
}

[[nodiscard]] ScanResult badRangeScanResult(u8 kind, AssetId assetId, SourceRange goodRange, SourceRange badRange) {
  switch (kind) {
    case 0:
      return ScanResult{
          .assets = {MiscAsset{.metadata = badRangeMetadata(assetId, "Bad Asset Range", badRange)}},
      };

    case 1: {
      auto metadata = badRangeMetadata(assetId, "Bad Item Range", goodRange);
      return ScanResult{
          .assets = {MiscAsset{.metadata = std::move(metadata)}},
          .sourceMap =
              SourceMap{{
                  SourceAnnotation{
                      .id = SourceAnnotationId{0},
                      .range = badRange,
                      .role = SourceRole::DataBlock,
                      .label = "Bad Annotation",
                  },
              }},
      };
    }

    case 2:
      return ScanResult{
          .assets = {SequenceProgramAsset{
              .metadata = badRangeMetadata(assetId, "Bad Command Range", goodRange),
              .program =
                  SequenceProgram{
                      .dialect = DialectId{.value = "probe"},
                      .timebase = Timebase{.ppqn = 48},
                      .tracks = {TrackProgram{
                          .id = TrackId{0},
                          .commands = {SourceCommand{
                              .id = CommandId{0},
                              .range = badRange,
                          }},
                      }},
                  },
          }},
      };

    case 3:
      return ScanResult{
          .assets = {SampleCollectionAsset{
              .metadata = badRangeMetadata(assetId, "Bad Sample Range", goodRange),
              .samples =
                  SampleCollection{
                      .samples = {Sample{
                          .name = "Bad Sample",
                          .encodedData = badRange,
                      }},
                  },
          }},
      };

    case 4:
      return ScanResult{
          .diagnostics = {Diagnostic{
              .severity = Severity::Warning,
              .message = "bad range diagnostic",
              .range = badRange,
          }},
      };

    case 5:
      return ScanResult{
          .extractedSources = {ExtractedSource{
              .file = SourceFile{.name = "bad-range.child"},
              .bytes = {0xbb},
              .origin = badRange,
          }},
      };

    default:
      return {};
  }
}

void scanCommitRejectsOutOfBoundsScanResultRanges() {
  struct BadRangeCase {
    u8 kind = 0;
    std::string_view message;
  };

  const std::array<BadRangeCase, 6> cases{{
      BadRangeCase{
          .kind = 0,
          .message = "Scan result contained asset metadata range outside source bounds (source 0, offset 3, size 1, "
                     "source size 2)",
      },
      BadRangeCase{
          .kind = 1,
          .message =
              "Scan result contained source annotation range outside source bounds (source 0, offset 3, size 1, source "
              "size 2)",
      },
      BadRangeCase{
          .kind = 2,
          .message = "Scan result contained sequence command range outside source bounds (source 0, offset 3, size 1, "
                     "source size 2)",
      },
      BadRangeCase{
          .kind = 3,
          .message = "Scan result contained sample encoded data range outside source bounds (source 0, offset 3, size "
                     "1, source size 2)",
      },
      BadRangeCase{
          .kind = 4,
          .message = "Scan result contained diagnostic range outside source bounds (source 0, offset 3, size 1, source "
                     "size 2)",
      },
      BadRangeCase{
          .kind = 5,
          .message = "Scan result contained extracted source origin range outside source bounds (source 0, offset 3, "
                     "size 1, source size 2)",
      },
  }};

  for (const auto& testCase : cases) {
    SourceStore sources;
    const auto source = sources.add(SourceFile{.name = "bad-range.probe"}, {0xf7, testCase.kind});
    ScanIdAllocator ids;
    ScanResult result = badRangeScanResult(testCase.kind, ids.nextAssetId(), sources.reader(source).range(0, 2),
                                           sources.reader(source).range(3, 1));
    normalizeScanResult(result, ids);
    const ScanCommit commit = ScanCommit::fromScanResult(sources.source(source), std::move(result));

    std::string message;
    try {
      AssetStore assets;
      commit.validate(sources, assets);
    } catch (const std::invalid_argument& ex) {
      message = ex.what();
    }
    expect(message == testCase.message, "scan commit should reject out-of-bounds source ranges");
  }
}

void scanCommitRejectsRangeLessSourceAnnotations() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "range-less-annotation.probe"}, {0xaa});
  ScanResult result{
      .assets = {MiscAsset{
          .metadata = badRangeMetadata(AssetId{0}, "Range-Less Annotation Fixture", sources.reader(source).range(0, 1)),
      }},
      .sourceMap =
          SourceMap{{
              SourceAnnotation{
                  .id = SourceAnnotationId{0},
                  .role = SourceRole::DataBlock,
                  .label = "Range-Less Annotation",
              },
          }},
  };
  const ScanCommit commit = ScanCommit::fromScanResult(sources.source(source), std::move(result));

  std::string message;
  try {
    AssetStore assets;
    commit.validate(sources, assets);
  } catch (const std::invalid_argument& ex) {
    message = ex.what();
  }
  expect(message == "Scan result contained source annotation without a primary source range",
         "scan commit should reject source annotations without primary ranges");
}

void scanCommitRejectsDanglingSourceAnnotationReferences() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "dangling-annotation-ref.probe"}, {0xaa});
  const SourceRange range = sources.reader(source).range(0, 1);

  const auto validate = [&](ScanResult result) {
    const ScanCommit commit = ScanCommit::fromScanResult(sources.source(source), std::move(result));
    std::string message;
    try {
      AssetStore assets;
      commit.validate(sources, assets);
    } catch (const std::invalid_argument& ex) {
      message = ex.what();
    }
    return message;
  };

  expect(validate(ScanResult{
             .sourceMap = SourceMap{{
                 SourceAnnotation{
                     .id = SourceAnnotationId{0},
                     .range = range,
                     .role = SourceRole::Header,
                     .label = "Child",
                     .parent = SourceAnnotationId{99},
                 },
             }},
         }) == "Scan result contained source annotation with missing parent annotation id 99",
         "scan commit should reject source annotations with dangling parents");

  expect(validate(ScanResult{
             .sourceMap = SourceMap{{
                 SourceAnnotation{
                     .id = SourceAnnotationId{0},
                     .range = range,
                     .role = SourceRole::Header,
                     .label = "Link",
                     .links = {SourceLink{
                         .role = SourceLinkRole::Related,
                         .target = SourceTarget{SourceAnnotationId{99}},
                     }},
                 },
             }},
         }) == "Scan result contained source annotation link to missing annotation id 99",
         "scan commit should reject source annotation links with dangling annotation targets");

  expect(validate(ScanResult{
             .sourceMap = SourceMap{{
                 SourceAnnotation{
                     .id = SourceAnnotationId{0},
                     .range = range,
                     .role = SourceRole::Header,
                     .label = "Header",
                 },
             }},
             .diagnostics = {Diagnostic{
                 .severity = Severity::Warning,
                 .message = "dangling diagnostic",
                 .range = range,
                 .annotation = SourceAnnotationId{99},
             }},
         }) == "Scan result contained diagnostic for missing source annotation id 99",
         "scan commit should reject diagnostics with dangling annotation anchors");
}

void snapshotValidationReportsWrongTypeCollectionReferences() {
  SessionSnapshotBuilder builder;
  builder.assets.push_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{0},
              .format = "ProbeSnapshot",
              .name = "Sequence",
          },
      .program =
          SequenceProgram{
              .dialect = DialectId{.value = "probe"},
              .timebase = Timebase{.ppqn = 48},
          },
  });
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Wrong Type",
      .key = CollectionKey{.resolver = "ProbeSnapshot", .value = "wrong-type"},
      .instrumentSets = {AssetId{0}},
  });

  const auto snapshot = builder.finish();
  const auto report = validateSessionSnapshot(snapshot);
  expect(report.hasErrors(), "snapshot validation should report wrong-type collection references");
  expect(report.findings().size() == 1, "snapshot validation should report the wrong reference once");
  expect(report.findings()[0].message == "Collection referenced missing or wrong-type instrument-set asset id 0",
         "snapshot validation should describe the wrong collection role");
}

void sessionReportsDesiredCollectionMissingAssetReferences() {
  Session session;
  session.formats().add(missingAssetCollectionResolverModule());

  session.addSource(SourceFile{.name = "missing-refs.probe"}, {0x00});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.collections().size() == 1, "resolver should still publish the collection shell");
  expect(project.collections()[0].status == CollectionStatus::Incomplete,
         "collection with missing asset references should be incomplete");
  expect(!project.collections()[0].sequence, "missing sequence reference should be stripped");
  expect(project.collections()[0].instrumentSets.empty(), "missing instrument reference should be stripped");
  expect(project.collections()[0].sampleCollections.empty(), "missing sample reference should be stripped");
  expect(project.collections()[0].miscAssets.empty(), "missing misc reference should be stripped");
  expect(project.collections()[0].issues.size() == 4, "missing references should be recorded as collection issues");
  static_cast<void>(diagnosticWithMessage(
      project.diagnostics(),
      "Collection resolver 'ProbeMissingRefs' returned sequence asset id 99 that does not exist"));
  static_cast<void>(diagnosticWithMessage(project.diagnostics(),
                                          "Collection resolver 'ProbeMissingRefs' returned instrument-set asset id 98 "
                                          "that does not exist"));
  static_cast<void>(diagnosticWithMessage(
      project.diagnostics(), "Collection resolver 'ProbeMissingRefs' returned sample-collection asset id 97 that does "
                             "not exist"));
  static_cast<void>(diagnosticWithMessage(
      project.diagnostics(), "Collection resolver 'ProbeMissingRefs' returned misc asset id 96 that does not exist"));
}

void sessionReportsDesiredCollectionWrongTypeReferences() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.formats().add(wrongTypeCollectionResolverModule());
  session.dialects().add(probeSequenceDialect());

  session.addSource(SourceFile{.name = "wrong-type.probe"}, {0xaa});
  const SessionSnapshot project = session.scanPendingSources();
  const auto found = std::ranges::find_if(project.collections(), [](const Collection& collection) {
    return collection.key.resolver == "ProbeWrongTypeRefs";
  });
  expect(found != project.collections().end(), "wrong-type resolver should publish a collection shell");
  expect(found->status == CollectionStatus::Incomplete,
         "collection with wrong-type references should be incomplete");
  expect(found->instrumentSets.empty(), "wrong-type instrument reference should be stripped");
  expect(found->sampleCollections.empty(), "wrong-type sample reference should be stripped");
  expect(found->miscAssets.empty(), "wrong-type misc reference should be stripped");
  expect(found->issues.size() == 3, "wrong-type references should be recorded as collection issues");

  static_cast<void>(diagnosticWithMessage(project.diagnostics(),
                                          "Collection resolver 'ProbeWrongTypeRefs' returned instrument-set asset id 0 "
                                          "that is not an instrument-set asset"));
  static_cast<void>(diagnosticWithMessage(
      project.diagnostics(),
      "Collection resolver 'ProbeWrongTypeRefs' returned sample-collection asset id 0 that is not a "
      "sample-collection asset"));
  static_cast<void>(diagnosticWithMessage(project.diagnostics(),
                                          "Collection resolver 'ProbeWrongTypeRefs' returned misc asset id 0 that is "
                                          "not a misc asset"));
}

void sessionReportsDuplicateDesiredCollectionKeys() {
  Session session;
  session.formats().add(duplicateKeyCollectionResolverModule());

  session.addSource(SourceFile{.name = "duplicate-keys.probe"}, {0x00});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.collections().size() == 1, "duplicate resolver keys should keep the first collection only");
  expect(project.collections()[0].name == "First", "first duplicate-key collection should be preserved");
  static_cast<void>(diagnosticWithMessage(
      project.diagnostics(), "Collection resolver 'ProbeDuplicateKeys' returned duplicate collection key 'same-key'"));
}

void sourceStoreRejectsMissingOrRemovedDerivedParents() {
  SourceStore store;

  bool missingParentFailed = false;
  try {
    static_cast<void>(store.addDerived(SourceFile{.name = "missing.child"}, {0xbb}, SourceId{99}, std::nullopt));
  } catch (const std::invalid_argument&) {
    missingParentFailed = true;
  }
  expect(missingParentFailed, "derived source parent must already exist");

  const auto parent = store.add(SourceFile{.name = "parent"}, {0xaa});
  static_cast<void>(store.removeFamily(parent));

  bool removedParentFailed = false;
  try {
    static_cast<void>(store.addDerived(SourceFile{.name = "removed.child"}, {0xbb}, parent, std::nullopt));
  } catch (const std::invalid_argument&) {
    removedParentFailed = true;
  }
  expect(removedParentFailed, "derived source parent must still be active");
}

void sessionSnapshotFinalizationReportsDuplicateIds() {
  SessionSnapshotBuilder builder;
  builder.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{7},
              .format = "Probe",
              .name = "First",
              .range = probeRange(4, 1),
          },
  });
  builder.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{7},
              .format = "Probe",
              .name = "Duplicate",
              .range = probeRange(8, 1),
          },
  });
  builder.collections.push_back(Collection{
      .id = CollectionId{3},
      .name = "First",
      .miscAssets = {AssetId{7}},
  });
  builder.collections.push_back(Collection{
      .id = CollectionId{3},
      .name = "Duplicate",
      .miscAssets = {AssetId{7}},
  });

  const SessionSnapshot project = builder.finish();

  expect(assetById(project, AssetId{7}) == &project.assets()[0],
         "duplicate asset id lookup should keep the first asset");
  expect(collectionById(project, CollectionId{3}) == &project.collections()[0],
         "duplicate collection id lookup should keep the first collection");
  expect(project.diagnostics().size() == 2, "snapshot finalization should report duplicate ids");

  const auto& assetDiagnostic = diagnosticWithMessage(project.diagnostics(), "Duplicate asset id 7 in SessionSnapshot");
  expect(assetDiagnostic.severity == Severity::Error, "duplicate asset id should be reported as an error");
  expect(assetDiagnostic.range && sameRange(*assetDiagnostic.range, probeRange(8, 1)),
         "duplicate asset id diagnostic should point at the conflicting asset");

  const auto& collectionDiagnostic =
      diagnosticWithMessage(project.diagnostics(), "Duplicate collection id 3 in SessionSnapshot");
  expect(collectionDiagnostic.severity == Severity::Error, "duplicate collection id should be reported as an error");
  expect(!collectionDiagnostic.range.has_value(), "collection id diagnostics should not invent a source range");
}

void sessionSnapshotCollectionAssetResolutionProvidesTypedExportInputs() {
  SessionSnapshotBuilder builder;
  builder.assets.emplace_back(SequenceProgramAsset{
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
  builder.assets.emplace_back(InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Instruments",
          },
  });
  builder.assets.emplace_back(SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Samples",
          },
  });
  builder.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{3},
              .format = "Probe",
              .name = "Misc",
          },
  });
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Full",
      .sequence = AssetId{0},
      .instrumentSets = {AssetId{1}, AssetId{41}},
      .sampleCollections = {AssetId{2}, AssetId{42}},
      .miscAssets = {AssetId{3}, AssetId{43}},
  });
  builder.collections.push_back(Collection{
      .id = CollectionId{1},
      .name = "Samples Only",
      .sampleCollections = {AssetId{2}},
  });

  const SessionSnapshot project = builder.finish();

  const auto full = resolveCollectionAssets(project, CollectionId{0});
  expect(full.collection == &project.collections()[0], "collection asset resolver should preserve the collection");
  expect(full.sequenceProgram == std::get_if<SequenceProgramAsset>(&project.assets()[0]),
         "collection asset resolver should resolve the typed sequence program asset");
  expect(full.instrumentSets.size() == 1 &&
             full.instrumentSets[0] == std::get_if<InstrumentSetAsset>(&project.assets()[1]),
         "collection asset resolver should resolve typed instrument set assets");
  expect(full.sampleCollections.size() == 1 &&
             full.sampleCollections[0] == std::get_if<SampleCollectionAsset>(&project.assets()[2]),
         "collection asset resolver should resolve typed sample collection assets");
  expect(full.miscAssets.size() == 1 && full.miscAssets[0] == std::get_if<MiscAsset>(&project.assets()[3]),
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
  expect(samplesOnly.collection == &project.collections()[1],
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

void assetStoreRebuildsLookupIndexAfterRemoval() {
  AssetStore assets;
  std::vector<Asset> firstSourceAssets;
  firstSourceAssets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{0},
              .format = "Probe",
              .name = "Sequence",
          },
  });
  firstSourceAssets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Misc",
          },
  });
  assets.append(std::move(firstSourceAssets), SourceId{0});

  std::vector<Asset> secondSourceAssets;
  secondSourceAssets.emplace_back(SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Samples",
          },
  });
  assets.append(std::move(secondSourceAssets), SourceId{1});

  expect(assets.findAs<SampleCollectionAsset>(AssetId{2}) == std::get_if<SampleCollectionAsset>(&assets.all()[2]),
         "asset store should look up assets by id before removal");

  const auto removed = assets.removeForSources({SourceId{0}});
  expect(removed.contains(0) && removed.contains(1) && removed.size() == 2,
         "asset store removal should report assets owned by the removed source");
  expect(!assets.contains(AssetId{0}) && !assets.contains(AssetId{1}),
         "asset store should remove deleted asset ids from the lookup index");
  expect(assets.all().size() == 1 && assets.findAs<SampleCollectionAsset>(AssetId{2}) ==
                                      std::get_if<SampleCollectionAsset>(&assets.all()[0]),
         "asset store lookup index should be rebuilt after removal compacts the asset vector");
}

void sessionAddsSourceFromPath() {
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

  const SessionSnapshot project = session.scanPendingSources();
  expect(project.collections().size() == 1, "path source should scan through registered modules");
  expect(project.sources().front().path == path, "session snapshot should preserve path source metadata");

  std::filesystem::remove(path);
}

void sessionExportsAllCollections() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.dialects().add(probeSequenceDialect());

  session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  const SessionSnapshot project = session.scanPendingSources();
  expect(project.collections().size() == 2, "probe sources should produce two collections");

  const auto exports = session.exportAllCollections(ExportRequest{
      .kinds = {ExportKind::Midi},
  });
  expect(exports.size() == project.collections().size(), "all-collection export should cover every collection");

  for (size_t i = 0; i < exports.size(); ++i) {
    expect(exports[i].collection == project.collections()[i].id,
           "all-collection export should preserve collection ids in project order");
    expect(exports[i].artifacts.size() == 1, "probe MIDI export should return one artifact per collection");
    expect(exports[i].artifacts[0].filename == project.collections()[i].name + ".mid",
           "collection export should keep collection-derived artifact names");
    expect(exports[i].artifacts[0].mediaType == "audio/midi", "collection export should keep artifact media types");
    expect(exports[i].artifacts[0].diagnostics.empty(),
           "registered probe sequence exports should not report missing dialect diagnostics");
  }
}

}  // namespace

void runValueSessionTests() {
  sessionScansValuesAndDerivedSources();
  sessionReportsUnregisteredSequenceDialect();
  sessionScansIndividualSourcesWithoutDuplicating();
  sessionKeepsScannerKnownCollectionsWithoutResolver();
  sessionMatchesCollectionsAcrossSeparateSourceScans();
  sessionMaterializesResolvedCollectionsWithStableAssets();
  sessionRemovesSourceFamilyAndDiscoveredData();
  sessionRemovalUpdatesCrossSourceCollectionLifecycle();
  sessionResolverFailureDoesNotWipeExistingCollections();
  sessionMarksCollectionsStaleWhenRemovalCannotReconcile();
  sessionRejectsLateRegistryMutation();
  sessionRejectsDuplicateAssetIdsAtScanCommit();
  sessionRejectsExtractedSourcesWithMissingParents();
  sessionRejectsMatchFactsForMissingAssets();
  sessionRejectsSourceScopedMatchFactsForMissingSources();
  scanValidationReportsMultipleAdmissionErrors();
  scanCommitRejectsOutOfBoundsScanResultRanges();
  scanCommitRejectsRangeLessSourceAnnotations();
  scanCommitRejectsDanglingSourceAnnotationReferences();
  snapshotValidationReportsWrongTypeCollectionReferences();
  sessionReportsDesiredCollectionMissingAssetReferences();
  sessionReportsDesiredCollectionWrongTypeReferences();
  sessionReportsDuplicateDesiredCollectionKeys();
  sourceStoreRejectsMissingOrRemovedDerivedParents();
  sessionSnapshotFinalizationReportsDuplicateIds();
  sessionSnapshotCollectionAssetResolutionProvidesTypedExportInputs();
  assetStoreRebuildsLookupIndexAfterRemoval();
  sessionAddsSourceFromPath();
  sessionExportsAllCollections();
}

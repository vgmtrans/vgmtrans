/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "SessionSnapshotBuilder.h"

#include "value/session/SessionState.h"
#include "value/validation/ScanValidation.h"

namespace {

[[nodiscard]] std::string firstValidationMessage(ValidationReport report) {
  return report.empty() ? std::string{} : report.diagnostics().front().message;
}

void sessionScansValuesAndDerivedSources() {
  Session session;
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());
  session.registerFormat(probeMiscModule());

  const auto sourceId = session.addSource(SourceFile{.name = "probe.spc"}, {0xaa, 0x34, 0x12});
  expect(sourceId == SourceId{0}, "first source should get SourceId 0");

  session.scanPendingSources();

  SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.sources().size() == 2, "scan should include extracted derived source");
  expect(snapshot.source(sourceId) == &snapshot.sources()[0], "session snapshot should find a source by stable id");
  expect(snapshot.source(SourceId{99}) == nullptr, "session snapshot should return null for a missing source id");
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
  expect(snapshot.asset(sequence->metadata.id) == &snapshot.assets()[0],
         "session snapshot should find an asset by stable id");
  expect(snapshot.asset<SequenceProgramAsset>(sequence->metadata.id) == sequence,
         "session snapshot should find a sequence program asset by stable id");
  expect(snapshot.asset<MiscAsset>(sequence->metadata.id) == nullptr,
         "session snapshot should reject asset id lookups with the wrong value type");
  expect(snapshot.asset(AssetId{99}) == nullptr, "session snapshot should return null for a missing asset id");
  expect(snapshot.asset<SequenceProgramAsset>(AssetId{99}) == nullptr,
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
  expect(snapshot.collection(snapshot.collections()[0].id) == &snapshot.collections()[0],
         "session snapshot should find a collection by stable id");
  expect(snapshot.collection(CollectionId{99}) == nullptr,
         "session snapshot should return null for a missing collection id");

  const auto* misc = std::get_if<MiscAsset>(&snapshot.assets()[1]);
  expect(misc != nullptr, "second asset should be misc from derived source");
  expect(metadata(snapshot.assets()[1]).id == AssetId{1}, "missing asset id should be assigned");

  session.scanPendingSources();

  snapshot = session.snapshot();
  expect(snapshot.sources().size() == 2, "pending-source scan should not duplicate already-scanned derived sources");
  expect(snapshot.assets().size() == 2, "pending-source scan should not duplicate already-scanned assets");
  expect(snapshot.collections().size() == 1, "pending-source scan should not duplicate already-resolved collections");
}

void sessionSharesOneImmutableSnapshotPerRevision() {
  Session session;
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());

  const auto firstSource = session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  const SessionSnapshot beforeScan = session.snapshot();
  const SessionSnapshot beforeScanCopy = session.snapshot();
  expect(&beforeScan.sources() == &beforeScanCopy.sources(),
         "repeated snapshot reads should share the current immutable revision");
  expect(beforeScan.assets().empty(), "unscanned snapshot revision should not contain assets");

  session.scanPendingSources();
  const SessionSnapshot afterScan = session.snapshot();
  expect(&afterScan.sources() != &beforeScan.sources(),
         "scanning should publish new snapshot storage on the next read");
  expect(afterScan.assets().size() == 1, "scanned snapshot revision should contain the discovered asset");
  expect(beforeScan.assets().empty(), "publishing a new revision should not mutate an older snapshot");

  const SessionSnapshot afterScanCopy = afterScan;
  expect(&afterScan.assets() == &afterScanCopy.assets() && &afterScan.sourceMap() == &afterScanCopy.sourceMap(),
         "copying a snapshot should share its complete immutable backing");

  session.scanSource(firstSource);
  const SessionSnapshot afterNoOpScan = session.snapshot();
  expect(&afterNoOpScan.assets() == &afterScan.assets(),
         "a no-op scan should retain the already materialized snapshot revision");

  const auto secondSource = session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  const SessionSnapshot afterAdd = session.snapshot();
  expect(&afterAdd.sources() != &afterScan.sources(),
         "adding a source should invalidate the materialized snapshot revision");
  expect(afterAdd.sources().size() == afterScan.sources().size() + 1 && afterAdd.assets().size() == 1,
         "the post-add revision should include the pending source without changing scanned assets");
  expect(afterScan.sources().size() == 2 && afterScan.assets().size() == 1,
         "adding a source should leave the previous snapshot revision stable");
  expect(&afterAdd.assets().front() == &afterScan.assets().front() &&
             &afterAdd.matchFacts().front() == &afterScan.matchFacts().front() &&
             &afterAdd.sourceMap().annotations().front() == &afterScan.sourceMap().annotations().front(),
         "a new revision should reuse unchanged admitted scan values");

  session.scanPendingSources();
  const SessionSnapshot afterSecondScan = session.snapshot();
  expect(afterSecondScan.assets().size() == 2, "scanning the pending source should append its discovered asset");
  expect(&afterSecondScan.assets().front() == &afterScan.assets().front() &&
             &afterSecondScan.matchFacts().front() == &afterScan.matchFacts().front() &&
             &afterSecondScan.sourceMap().annotations().front() == &afterScan.sourceMap().annotations().front(),
         "appending scan data should preserve the backing of earlier chunks");

  session.removeSource(secondSource);
  const SessionSnapshot afterRemoval = session.snapshot();
  expect(afterRemoval.assets().size() == 1 && afterSecondScan.assets().size() == 2,
         "removing a source should publish a new revision without changing its predecessor");
  expect(&afterRemoval.assets().front() == &afterScan.assets().front() &&
             &afterRemoval.matchFacts().front() == &afterScan.matchFacts().front() &&
             &afterRemoval.sourceMap().annotations().front() == &afterScan.sourceMap().annotations().front(),
         "removal should retain the backing of unaffected chunks");
}

void sessionReportsUnregisteredSequenceDialect() {
  Session session;
  session.registerFormat(probeSequenceModule());

  session.addSource(SourceFile{.name = "missing-dialect.probe"}, {0xaa});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
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
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());

  const auto first = session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  session.scanSource(first);
  SessionSnapshot project = session.snapshot();
  expect(project.assets().size() == 1, "source scan should add assets from the requested source");
  expect(project.collections().size() == 1, "source scan should resolve collections after the scan transaction");

  session.scanSource(first);

  project = session.snapshot();
  expect(project.assets().size() == 1, "repeat source scan should not duplicate already-scanned assets");
  expect(project.collections().size() == 1, "repeat source scan should not duplicate already-resolved collections");

  const auto second = session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  session.scanSource(second);
  project = session.snapshot();
  expect(project.assets().size() == 2, "later source scan should preserve previous assets and add the new source");
  expect(project.collections().size() == 2,
         "later source scan should preserve previous collections and add the new one");

  session.scanPendingSources();

  project = session.snapshot();
  expect(project.assets().size() == 2, "pending-source scan should skip already-scanned user sources");
  expect(project.collections().size() == 2, "pending-source scan should leave existing collections unchanged");
}

void sessionClosesSourceFamiliesWhenScansFindNoAssets() {
  Session session;
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());
  session.registerFormat(FormatModule{
      .name = "ProbeEmptyExtractor",
      .canScan = [](const SourceFile&, std::span<const u8> bytes) { return !bytes.empty() && bytes[0] == 0x00; },
      .scan =
          [](const ScanInput& input) {
            ScanResult result;
            if (!input.source.derived()) {
              result.extractedSources.push_back(ExtractedSource{
                  .file = SourceFile{.name = input.source.name + ".empty-child"},
                  .bytes = {0x00},
                  .origin = input.reader.range(0, 1),
              });
            }
            return result;
          },
  });

  const SourceId detected = session.addSource(SourceFile{.name = "detected.probe"}, {0xaa});
  const SourceId empty = session.addSource(SourceFile{.name = "empty.probe"}, {0x00});
  session.scanPendingSources();

  SessionSnapshot project = session.snapshot();
  expect(project.source(detected) != nullptr && project.assets().size() == 1,
         "a source family with a detected asset should remain open");
  expect(project.source(empty) == nullptr,
         "a source family should close when neither it nor an extracted child contains detected assets");
  expect(project.sources().size() == 2,
         "closing an empty family should preserve the detected source and its extracted child");
  expect(!session.sources().contains(empty), "closing an empty scan should release its source bytes");

  const SourceId individuallyScanned = session.addSource(SourceFile{.name = "individual-empty.probe"}, {0x00});
  session.scanSource(individuallyScanned);
  project = session.snapshot();
  expect(project.source(individuallyScanned) == nullptr,
         "an individual scan should also close a source family without detected assets");
}

void sessionKeepsScannerKnownCollectionsWithoutResolver() {
  Session session;
  session.registerFormat(probeExplicitCollectionModule(), probeSequenceDialect());

  const auto source = session.addSource(SourceFile{.name = "explicit.probe"}, {0xab});
  session.scanSource(source);
  SessionSnapshot project = session.snapshot();
  expect(project.matchFacts().empty(), "explicit scanner-known collection should not need match facts");
  expect(project.collections().size() == 1, "explicit scanner-known collection should be published");
  expect(project.collections()[0].key.resolver == "ProbeExplicit",
         "explicit scanner-known collection should use its scanner resolver key");

  session.removeSource(source);

  project = session.snapshot();
  expect(project.collections().empty(), "explicit scanner-known collection should disappear with its source");
}

void sessionMatchesCollectionsAcrossSeparateSourceScans() {
  Session session;
  session.registerFormat(probeBankSequenceModule(), probeSequenceDialect());
  session.registerFormat(probeBankInstrumentModule());

  const auto instrument = session.addSource(SourceFile{.name = "bank-7.instr"}, {0xdd, 7});
  session.scanSource(instrument);
  SessionSnapshot project = session.snapshot();
  expect(project.assets().size() == 1, "instrument scan should add its asset immediately");
  expect(project.collections().size() == 1, "resolver should keep an incomplete collection for a partial match");
  expect(project.collections()[0].status == CollectionStatus::Incomplete,
         "instrument-only bank collection should be marked incomplete");
  expect(project.collections()[0].instrumentSets.size() == 1,
         "instrument-only bank collection should reference the instrument set");
  const CollectionId bankCollection = project.collections()[0].id;

  const auto sequence = session.addSource(SourceFile{.name = "bank-7.seq"}, {0xcc, 7});
  session.scanSource(sequence);
  project = session.snapshot();
  expect(project.assets().size() == 2, "second source scan should add the matching sequence asset");
  expect(project.collections().size() == 1, "matching facts should update the existing bank collection");
  expect(project.collections()[0].id == bankCollection, "resolver update should preserve the collection id");
  expect(project.collections()[0].status == CollectionStatus::Complete,
         "bank collection should become complete when sequence and instruments are both present");
  expect(project.collections()[0].sequence.has_value(), "completed bank collection should reference the sequence");
  expect(project.collections()[0].instrumentSets.size() == 1,
         "completed bank collection should retain the instrument reference");
}

void sessionRemovesSourceFamilyAndDiscoveredData() {
  Session session;
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());
  session.registerFormat(probeMiscModule());

  const auto source = session.addSource(SourceFile{.name = "remove-me.probe"}, {0xaa, 0x34});
  session.scanSource(source);
  SessionSnapshot project = session.snapshot();
  expect(project.sources().size() == 2, "fixture should scan one user source and one derived source");
  expect(project.assets().size() == 2, "fixture should scan user and derived assets");
  expect(project.matchFacts().size() == 1, "fixture should publish a collection match fact");
  expect(project.collections().size() == 1, "fixture should publish one collection");
  expect(project.diagnostics().size() == 1, "fixture should publish one source-backed diagnostic");

  session.removeSource(source);

  project = session.snapshot();
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
    session.scanSource(source);
  } catch (const std::out_of_range&) {
    scanRemovedSourceFailed = true;
  }
  expect(scanRemovedSourceFailed, "removed sources should not be scannable");

  const auto replacement = session.addSource(SourceFile{.name = "replacement.probe"}, {0xaa});
  expect(replacement == SourceId{2}, "source ids should not be reused after removing a source family");
  session.scanPendingSources();
  project = session.snapshot();
  expect(project.sources().size() == 2, "replacement scan should add a new derived source");
  expect(project.sources()[0].id == replacement, "replacement user source should keep its new stable id");
}

void sessionRemovesSourceFamilyWithItsLastAsset() {
  Session session;
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());
  session.registerFormat(probeMiscModule());

  const SourceId source = session.addSource(SourceFile{.name = "remove-assets.probe"}, {0xaa, 0x34});
  session.scanSource(source);
  SessionSnapshot project = session.snapshot();
  expect(project.sources().size() == 2 && project.assets().size() == 2,
         "asset removal fixture should publish assets from a user and derived source");
  const AssetId sequence = metadata(project.assets()[0]).id;
  const AssetId misc = metadata(project.assets()[1]).id;

  const std::array firstRemoval{sequence};
  session.removeAssets(firstRemoval);
  project = session.snapshot();
  expect(project.sources().size() == 2 && project.source(source) != nullptr,
         "a source family should remain while it still owns a detected asset");
  expect(project.assets().size() == 1 && metadata(project.assets().front()).id == misc,
         "removing one detected asset should preserve the other family asset");

  const std::array lastRemoval{misc};
  session.removeAssets(lastRemoval);
  project = session.snapshot();
  expect(project.sources().empty() && project.assets().empty(),
         "removing the last detected asset should close its entire source family");
  expect(project.collections().empty() && project.sourceMap().empty(),
         "closing the empty source family should remove its discovered data");

  session.removeAssets(lastRemoval);

  project = session.snapshot();
  expect(project.sources().empty() && project.assets().empty(),
         "removing an already removed asset should leave the session unchanged");
  session.scanPendingSources();
  project = session.snapshot();
  expect(project.sources().empty() && project.assets().empty(),
         "a source closed with its last asset should not be rescanned");
}

void sessionRemovalUpdatesCrossSourceCollectionLifecycle() {
  Session session;
  session.registerFormat(probeBankSequenceModule(), probeSequenceDialect());
  session.registerFormat(probeBankInstrumentModule());

  const auto instrument = session.addSource(SourceFile{.name = "bank-9.instr"}, {0xdd, 9});
  const auto sequence = session.addSource(SourceFile{.name = "bank-9.seq"}, {0xcc, 9});
  session.scanPendingSources();
  SessionSnapshot project = session.snapshot();
  expect(project.collections().size() == 1, "matching bank files should produce one collection");
  expect(project.collections()[0].status == CollectionStatus::Complete, "matched bank collection should be complete");
  const CollectionId collectionId = project.collections()[0].id;

  session.removeSource(instrument);

  project = session.snapshot();
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

  session.removeSource(sequence);

  project = session.snapshot();
  expect(project.sources().empty(), "removing the last matched source should leave no active sources");
  expect(project.assets().empty(), "removing the last matched source should leave no assets");
  expect(project.matchFacts().empty(), "removing the last matched source should leave no match facts");
  expect(project.collections().empty(), "resolver-owned discovered collection should disappear when no facts remain");
}

void sessionResolverFailureDoesNotWipeExistingCollections() {
  Session session;
  session.registerFormat(fragileProbeSequenceModule(), probeSequenceDialect());

  const auto first = session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  session.scanSource(first);
  SessionSnapshot project = session.snapshot();
  expect(project.collections().size() == 1, "initial scan should create a collection");
  const CollectionId originalCollection = project.collections()[0].id;

  session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  session.scanPendingSources();
  project = session.snapshot();
  expect(project.collections().size() == 1, "resolver failure should preserve previous collections");
  expect(project.collections()[0].id == originalCollection, "preserved collection should keep its id");
  static_cast<void>(diagnosticWithMessage(project.diagnostics(),
                                          "ProbeSequenceFragileResolver resolveCollections failed: resolver exploded"));
}

void sessionMarksCollectionsStaleWhenRemovalCannotReconcile() {
  Session session;
  session.registerFormat(fragileProbeSequenceModule(), probeSequenceDialect());

  const auto source = session.addSource(SourceFile{.name = "stale-on-failure.probe"}, {0xaa});
  session.scanSource(source);
  SessionSnapshot project = session.snapshot();
  expect(project.collections().size() == 1, "initial scan should create a collection");
  const CollectionId originalCollection = project.collections()[0].id;

  session.removeSource(source);

  project = session.snapshot();
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
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());

  session.addSource(SourceFile{.name = "sealed.probe"}, {0xaa});

  bool formatFailed = false;
  try {
    session.registerFormat(probeMiscModule());
  } catch (const std::logic_error&) {
    formatFailed = true;
  }
  expect(formatFailed, "format registry should be sealed after session mutation starts");

  Session scannedEmptySession;
  scannedEmptySession.scanPendingSources();

  bool emptyScanSealed = false;
  try {
    scannedEmptySession.registerFormat(probeSequenceModule());
  } catch (const std::logic_error&) {
    emptyScanSealed = true;
  }
  expect(emptyScanSealed, "format registry should also be sealed by an explicit scan");
}

void sessionRejectsDuplicateAssetIdsAtAdmission() {
  Session session;
  session.registerFormat(probeDuplicateAssetModule());

  session.addSource(SourceFile{.name = "duplicate.probe"}, {0xee});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
  expect(project.assets().empty(), "duplicate asset ids should reject the whole scan result before admission");
  expect(project.collections().empty(), "rejected duplicate asset scan should not create collections");
  expect(diagnosticWithMessage(project.diagnostics(),
                               "ProbeDuplicate scan failed: Scan result contained duplicate asset id 7")
             .code == "scan.asset.duplicate-id",
         "session admission should preserve structured validation diagnostics");
  expectDiagnosticRange(project.diagnostics(), "ProbeDuplicate scan failed: Scan result contained duplicate asset id 7",
                        SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void sessionRejectsExtractedSourcesWithMissingParents() {
  Session session;
  session.registerFormat(probeBadExtractedSourceModule());
  session.registerFormat(probeMiscModule());

  session.addSource(SourceFile{.name = "bad-derived-parent.probe"}, {0xf1});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
  expect(project.sources().size() == 1, "bad extracted source should not be added to the session");
  expect(project.assets().empty(), "bad extracted source should reject staged scan assets before admission");
  expectDiagnosticRange(
      project.diagnostics(),
      "ProbeBadExtracted scan failed: Scan result contained extracted source with missing parent source 99",
      SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void sessionRejectsMatchFactsForMissingAssets() {
  Session session;
  session.registerFormat(probeBadFactAssetModule());

  session.addSource(SourceFile{.name = "bad-fact-asset.probe"}, {0xf2});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
  expect(project.assets().empty(), "invalid match fact should reject the whole scan result before admission");
  expect(project.matchFacts().empty(), "invalid match fact should not be committed");
  expectDiagnosticRange(project.diagnostics(),
                        "ProbeBadFactAsset scan failed: Scan result contained a match fact for missing asset id 99",
                        SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void sessionRejectsSourceScopedMatchFactsForMissingSources() {
  Session session;
  session.registerFormat(probeBadFactSourceModule());

  session.addSource(SourceFile{.name = "bad-fact-source.probe"}, {0xf3});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
  expect(project.assets().empty(), "source-scoped invalid fact should reject the whole scan result before admission");
  expect(project.matchFacts().empty(), "source-scoped invalid fact should not be committed");
  expectDiagnosticRange(project.diagnostics(),
                        "ProbeBadFactSource scan failed: Scan result contained a match fact for missing source id 99",
                        SourceRange{.source = SourceId{0}, .offset = 0, .size = 1});
}

void scanValidationReportsMultipleAdmissionErrors() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "multi-error.probe"}, {0xaa});
  const auto foreignSource = sources.add(SourceFile{.name = "foreign.probe"}, {0xbb});
  const auto goodRange = sources.reader(source).range(0, 1);

  ScanResult result{
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
                                .range = sources.reader(foreignSource).range(0, 1),
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

  const auto report = validateScanResult(source, result, sources, {});
  expect(!report.empty(), "scan validation should report admission errors");

  bool sawDuplicateAsset = false;
  bool sawForeignSource = false;
  bool sawMissingFactAsset = false;
  bool sawMissingFactSource = false;
  for (const auto& diagnostic : report.diagnostics()) {
    sawDuplicateAsset = sawDuplicateAsset || diagnostic.message == "Scan result contained duplicate asset id 7";
    sawForeignSource = sawForeignSource || diagnostic.code == "scan.asset.foreign-source";
    sawMissingFactAsset =
        sawMissingFactAsset || diagnostic.message == "Scan result contained a match fact for missing asset id 99";
    sawMissingFactSource =
        sawMissingFactSource || diagnostic.message == "Scan result contained a match fact for missing source id 99";
  }
  expect(sawDuplicateAsset, "scan validation should report duplicate asset ids");
  expect(sawForeignSource, "scan validation should reject assets whose primary range belongs to another source");
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
          .sourceMap = SourceMap{{
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

void scanValidationRejectsOutOfBoundsScanResultRanges() {
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
    const auto message = firstValidationMessage(validateScanResult(source, result, sources, {}));
    expect(message == testCase.message, "scan validation should reject out-of-bounds source ranges");
  }
}

void scanValidationRejectsRangeLessSourceAnnotations() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "range-less-annotation.probe"}, {0xaa});
  ScanResult result{
      .assets = {MiscAsset{
          .metadata = badRangeMetadata(AssetId{0}, "Range-Less Annotation Fixture", sources.reader(source).range(0, 1)),
      }},
      .sourceMap = SourceMap{{
          SourceAnnotation{
              .id = SourceAnnotationId{0},
              .role = SourceRole::DataBlock,
              .label = "Range-Less Annotation",
          },
      }},
  };
  const auto message = firstValidationMessage(validateScanResult(source, result, sources, {}));
  expect(message == "Scan result contained source annotation without a primary source range",
         "scan validation should reject source annotations without primary ranges");
}

void scanValidationRejectsDanglingSourceAnnotationReferences() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "dangling-annotation-ref.probe"}, {0xaa});
  const SourceRange range = sources.reader(source).range(0, 1);

  const auto validate = [&](ScanResult result) {
    return firstValidationMessage(validateScanResult(source, result, sources, {}));
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
         "scan validation should reject source annotations with dangling parents");

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
         "scan validation should reject source annotation links with dangling annotation targets");

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
         "scan validation should reject diagnostics with dangling annotation anchors");
}

void sessionReportsDesiredCollectionMissingAssetReferences() {
  Session session;
  session.registerFormat(missingAssetCollectionResolverModule());

  session.addSource(SourceFile{.name = "missing-refs.probe"}, {0x00});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
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
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());
  session.registerFormat(wrongTypeCollectionResolverModule());

  session.addSource(SourceFile{.name = "wrong-type.probe"}, {0xaa});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
  const auto found = std::ranges::find_if(project.collections(), [](const Collection& collection) {
    return collection.key.resolver == "ProbeWrongTypeRefs";
  });
  expect(found != project.collections().end(), "wrong-type resolver should publish a collection shell");
  expect(found->status == CollectionStatus::Incomplete, "collection with wrong-type references should be incomplete");
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
  session.registerFormat(duplicateKeyCollectionResolverModule());

  session.addSource(SourceFile{.name = "duplicate-keys.probe"}, {0x00});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
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
  const SharedSourceBytes retainedBytes = store.sharedBytes(parent);
  static_cast<void>(store.removeFamily(parent));
  expect(retainedBytes && *retainedBytes == std::vector<u8>{0xaa},
         "removing a source should preserve immutable bytes retained by an open inspection");

  bool removedParentFailed = false;
  try {
    static_cast<void>(store.addDerived(SourceFile{.name = "removed.child"}, {0xbb}, parent, std::nullopt));
  } catch (const std::invalid_argument&) {
    removedParentFailed = true;
  }
  expect(removedParentFailed, "derived source parent must still be active");
}

void sessionStateRebuildsLookupIndexAfterRemoval() {
  SessionState state;
  std::vector<Asset> firstSourceAssets;
  firstSourceAssets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{0},
              .format = "Probe",
              .name = "Sequence",
              .range = SourceRange{.source = SourceId{0}, .offset = 0, .size = 1},
          },
  });
  firstSourceAssets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Misc",
              .range = SourceRange{.source = SourceId{0}, .offset = 1, .size = 1},
          },
  });
  state.appendScan(SourceId{0}, ScanResult{
                                    .assets = std::move(firstSourceAssets),
                                });

  std::vector<Asset> secondSourceAssets;
  secondSourceAssets.emplace_back(SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Samples",
              .range = SourceRange{.source = SourceId{1}, .offset = 0, .size = 1},
          },
  });
  state.appendScan(SourceId{1}, ScanResult{
                                    .assets = std::move(secondSourceAssets),
                                });

  expect(state.asset<SampleCollectionAsset>(AssetId{2}) == std::get_if<SampleCollectionAsset>(&state.assets()[2]),
         "session state should look up assets by id before removal");

  const std::array removedSources{SourceId{0}};
  state.removeSources(removedSources);
  expect(!state.containsAsset(AssetId{0}) && !state.containsAsset(AssetId{1}),
         "session state should remove deleted asset ids from the lookup index");
  expect(state.assets().size() == 1 &&
             state.asset<SampleCollectionAsset>(AssetId{2}) == std::get_if<SampleCollectionAsset>(&state.assets()[0]),
         "session state lookup index should be rebuilt after removal compacts the asset vector");
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
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());

  const auto sourceId = session.addSourceFromPath(path);
  expect(sourceId == SourceId{0}, "path source should get SourceId 0");
  expect(session.sources().source(sourceId).name == path.filename().string(),
         "path source should use the filename as source name");
  expect(session.sources().source(sourceId).path == path, "path source should preserve filesystem path");
  const std::array<u8, 3> expectedBytes{0xaa, 0x34, 0x12};
  expect(std::ranges::equal(session.sources().bytes(sourceId), expectedBytes),
         "path source should preserve file bytes");

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  expect(project.collections().size() == 1, "path source should scan through registered modules");
  expect(project.sources().front().path == path, "session snapshot should preserve path source metadata");

  std::filesystem::remove(path);
}

void sessionExportsAllCollections() {
  Session session;
  session.registerFormat(probeSequenceModule(), probeSequenceDialect());

  session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
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

void sessionExportsASequenceWithoutACollection() {
  Session session;
  auto format = probeSequenceModule();
  format.resolveCollections = {};
  session.registerFormat(std::move(format), probeSequenceDialect());

  session.addSource(SourceFile{.name = "loose.probe"}, {0xaa});
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.assets().size() == 1 && snapshot.collections().empty(),
         "standalone sequence fixture should scan without creating a collection");

  const AssetId sequence = metadata(snapshot.assets().front()).id;
  const Artifact artifact = session.exportSequenceMidi(sequence, SequenceExportRequest{});
  expect(artifact.filename == "loose.probe.mid", "session sequence export should use the standalone asset name");
  expect(artifact.diagnostics.empty() && artifact.bytes.size() >= 4 &&
             std::string(artifact.bytes.begin(), artifact.bytes.begin() + 4) == "MThd",
         "session should export an uncollected sequence as Standard MIDI");
}

void snapshotFindsTheFirstCollectionContainingAnAsset() {
  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(MiscAsset{.metadata = AssetMetadata{.id = AssetId{4}, .name = "Shared"}});
  builder.collections = {
      Collection{.id = CollectionId{8}, .name = "First", .miscAssets = {AssetId{4}}},
      Collection{.id = CollectionId{9}, .name = "Second", .miscAssets = {AssetId{4}}},
  };
  const SessionSnapshot snapshot = builder.finish();

  const auto* collection = snapshot.firstCollectionContaining(AssetId{4});
  expect(collection != nullptr && collection->id == CollectionId{8},
         "asset association lookup should preserve collection order");
  expect(snapshot.firstCollectionContaining(AssetId{99}) == nullptr,
         "asset association lookup should return null for an unassociated asset");
}

}  // namespace

void runValueSessionTests() {
  sessionScansValuesAndDerivedSources();
  sessionSharesOneImmutableSnapshotPerRevision();
  sessionReportsUnregisteredSequenceDialect();
  sessionScansIndividualSourcesWithoutDuplicating();
  sessionClosesSourceFamiliesWhenScansFindNoAssets();
  sessionKeepsScannerKnownCollectionsWithoutResolver();
  sessionMatchesCollectionsAcrossSeparateSourceScans();
  sessionRemovesSourceFamilyAndDiscoveredData();
  sessionRemovesSourceFamilyWithItsLastAsset();
  sessionRemovalUpdatesCrossSourceCollectionLifecycle();
  sessionResolverFailureDoesNotWipeExistingCollections();
  sessionMarksCollectionsStaleWhenRemovalCannotReconcile();
  sessionRejectsLateRegistryMutation();
  sessionRejectsDuplicateAssetIdsAtAdmission();
  sessionRejectsExtractedSourcesWithMissingParents();
  sessionRejectsMatchFactsForMissingAssets();
  sessionRejectsSourceScopedMatchFactsForMissingSources();
  scanValidationReportsMultipleAdmissionErrors();
  scanValidationRejectsOutOfBoundsScanResultRanges();
  scanValidationRejectsRangeLessSourceAnnotations();
  scanValidationRejectsDanglingSourceAnnotationReferences();
  sessionReportsDesiredCollectionMissingAssetReferences();
  sessionReportsDesiredCollectionWrongTypeReferences();
  sessionReportsDuplicateDesiredCollectionKeys();
  sourceStoreRejectsMissingOrRemovedDerivedParents();
  sessionStateRebuildsLookupIndexAfterRemoval();
  sessionAddsSourceFromPath();
  sessionExportsAllCollections();
  sessionExportsASequenceWithoutACollection();
  snapshotFindsTheFirstCollectionContainingAnAsset();
}

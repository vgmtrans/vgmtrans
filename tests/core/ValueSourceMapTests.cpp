/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"
#include "value/session/SessionState.h"
#include "value/validation/ScanValidation.h"

namespace {

void sourceMapBuilderRecordsAnnotationsFieldsAndLinks() {
  ScanIdAllocator ids;
  SourceMapBuilder builder([&ids]() { return ids.nextSourceAnnotationId(); });
  const SourceId source{3};
  const SourceRange headerRange{.source = source, .offset = 0, .size = 4};
  const SourceRange tableRange{.source = source, .offset = 8, .size = 8};

  const auto header =
      builder.header("Probe Header", headerRange)
          .field("Magic", SourceRange{.source = source, .offset = 0, .size = 1}, u8{0xaa}, SourceValueDisplay::Hex)
          .derived("Decoded", "yes")
          .link(SourceLinkRole::PointsTo, tableRange, "Table");
  const auto table = builder.table("Pointer Table", tableRange).parent(header.id());
  const auto command =
      builder
          .command("Pitch Bend Range", SourceRange{.source = source, .offset = 10, .size = 2}, SequenceSemantic::Pitch)
          .field("bend", SourceRange{.source = source, .offset = 10, .size = 1}, s8{-2},
                 SourceValueDisplay::SignedDecimal);

  const SourceMap sourceMap = builder.finish();
  expect(sourceMap.annotations().size() == 3, "source map should contain all builder annotations");

  const auto& headerAnnotation = sourceMap.get(header.id());
  expect(headerAnnotation.role == SourceRole::Header, "header helper should create a header annotation");
  expect(headerAnnotation.localKind == "probe-header", "source map should slugify annotation labels");
  expect(headerAnnotation.fields.size() == 2, "source map should preserve direct and derived fields");
  expect(std::get<u64>(headerAnnotation.fields[0].value) == 0xaa, "source map should preserve field values");
  expect(headerAnnotation.fields[0].display == SourceValueDisplay::Hex,
         "source map should preserve field display hints");
  expect(headerAnnotation.links.size() == 1 && headerAnnotation.links[0].role == SourceLinkRole::PointsTo,
         "source map should preserve structured links");
  expect(sourceMap.linksTo(SourceTarget{tableRange}) == std::vector<SourceAnnotationId>{header.id()},
         "source map should find annotations linking to a target range");

  expect(sourceMap.get(table.id()).parent == header.id(), "source map should preserve source containment");
  expect(sourceMap.get(command.id()).sequenceSemantic == SequenceSemantic::Pitch,
         "command helper should preserve sequence semantic");
  expect(sourceMap.get(command.id()).localKind == "pitch-bend-range",
         "command helper should slugify multi-word labels");
  expect(sourceMap.get(command.id()).fields.size() == 1 && sourceMap.get(command.id()).fields[0].name == "bend" &&
             std::get<s64>(sourceMap.get(command.id()).fields[0].value) == -2 &&
             sourceMap.get(command.id()).fields[0].display == SourceValueDisplay::SignedDecimal,
         "command annotation should own decoded operand fields for inspectors and HexView");

  expect(sourceMap.annotationsForSource(source).size() == 3, "source map should index annotations by source");
  expect(sourceMap.at(source, 10) == std::vector<SourceAnnotationId>{table.id(), command.id()},
         "source map should find annotations at a byte offset");
  expect(sourceMap.intersecting(SourceRange{.source = source, .offset = 10, .size = 1}) ==
             std::vector<SourceAnnotationId>{table.id(), command.id()},
         "HexView-style range lookup should return the same annotations that own decoded fields");
  expect(sourceMap.intersecting(SourceRange{.source = source, .offset = 9, .size = 1}) ==
             std::vector<SourceAnnotationId>{table.id()},
         "source map should find intersecting annotations");
  expect(sourceMap.containing(SourceRange{.source = source, .offset = 10, .size = 2}) ==
             std::vector<SourceAnnotationId>{table.id(), command.id()},
         "source map should find annotations containing a range");
  expect(sourceMap.withRole(source, SourceRole::Header) == std::vector<SourceAnnotationId>{header.id()},
         "source map should filter annotations by source role");
  expect(
      sourceMap.withSequenceSemantic(source, SequenceSemantic::Pitch) == std::vector<SourceAnnotationId>{command.id()},
      "source map should filter annotations by sequence semantic");
}

void sourceAnnotationsCarryOutlinePolicyForTreeConsumers() {
  ScanIdAllocator ids;
  SourceMapBuilder builder([&ids]() { return ids.nextSourceAnnotationId(); });
  const SourceId source{8};
  const auto root = builder.header("Header", SourceRange{.source = source, .offset = 0, .size = 4});
  const auto autoField =
      builder.annotation(SourceRole::Field, "Version", SourceRange{.source = source, .offset = 0, .size = 1})
          .parent(root.id());
  const auto shownField =
      builder.annotation(SourceRole::Field, "ADSR/Gain", SourceRange{.source = source, .offset = 1, .size = 3})
          .parent(root.id())
          .outline(SourceOutlinePolicy::Show);
  const auto hiddenHeader = builder.header("Internal Header", SourceRange{.source = source, .offset = 2, .size = 1})
                                .parent(root.id())
                                .outline(SourceOutlinePolicy::Hide);

  const SourceMap sourceMap = builder.finish();
  expect(sourceMap.get(root.id()).outline == SourceOutlinePolicy::Auto,
         "source annotations should default to automatic outline visibility");
  expect(sourceMap.get(autoField.id()).outline == SourceOutlinePolicy::Auto,
         "field annotations should preserve automatic outline policy for consumers to hide by default");
  expect(sourceMap.get(shownField.id()).outline == SourceOutlinePolicy::Show,
         "format code should be able to force structural documentation into Tree View");
  expect(sourceMap.get(hiddenHeader.id()).outline == SourceOutlinePolicy::Hide,
         "format code should be able to hide source-backed implementation details from Tree View");
  expect(sourceMap.childrenOf(root.id()) ==
             std::vector<SourceAnnotationId>{autoField.id(), shownField.id(), hiddenHeader.id()},
         "outline policy should not replace source-backed hierarchy");
}

void sourceMapRejectsDuplicateAnnotationIds() {
  bool sourceMapThrew = false;
  try {
    static_cast<void>(SourceMap{{
        SourceAnnotation{
            .id = SourceAnnotationId{7},
            .range = SourceRange{.source = SourceId{3}, .offset = 0, .size = 1},
            .role = SourceRole::Header,
            .label = "First",
        },
        SourceAnnotation{
            .id = SourceAnnotationId{7},
            .range = SourceRange{.source = SourceId{3}, .offset = 1, .size = 1},
            .role = SourceRole::Header,
            .label = "Second",
        },
    }});
  } catch (const std::logic_error&) {
    sourceMapThrew = true;
  }
  expect(sourceMapThrew, "source map should reject duplicate annotation ids");

  SourceMapBuilder builder([]() { return SourceAnnotationId{9}; });
  static_cast<void>(builder.header("First", SourceRange{.source = SourceId{3}, .offset = 0, .size = 1}));

  bool builderThrew = false;
  try {
    static_cast<void>(builder.header("Second", SourceRange{.source = SourceId{3}, .offset = 1, .size = 1}));
  } catch (const std::logic_error&) {
    builderThrew = true;
  }
  expect(builderThrew, "source map builder should reject duplicate annotation ids from its allocator");
}

void sessionStateRejectsCrossScanAnnotationIdCollisions() {
  SessionState state;
  state.appendScan(SourceId{1}, ScanResult{
                                    .sourceMap = SourceMap{{SourceAnnotation{
                                        .id = SourceAnnotationId{7},
                                        .range = SourceRange{.source = SourceId{1}, .offset = 0, .size = 1},
                                        .label = "First",
                                    }}},
                                });

  bool threw = false;
  try {
    state.appendScan(SourceId{2}, ScanResult{
                                      .sourceMap = SourceMap{{SourceAnnotation{
                                          .id = SourceAnnotationId{7},
                                          .range = SourceRange{.source = SourceId{2}, .offset = 0, .size = 1},
                                          .label = "Second",
                                      }}},
                                  });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "session state should reject annotation ids already owned by another scan");
  expect(state.sourceMap().annotations().size() == 1,
         "a rejected scan append should leave the existing annotations unchanged");
}

void sessionStatePreflightsSourceAnnotationIdCollisions() {
  SessionState state;
  state.appendScan(SourceId{1}, ScanResult{
                                    .sourceMap = SourceMap{{SourceAnnotation{
                                        .id = SourceAnnotationId{7},
                                        .range = SourceRange{.source = SourceId{1}, .offset = 0, .size = 1},
                                        .label = "Existing",
                                    }}},
                                });

  ScanResult result{
      .assets = {MiscAsset{.metadata = AssetMetadata{.id = AssetId{3}, .name = "Uncommitted"}}},
      .sourceMap = SourceMap{{SourceAnnotation{
          .id = SourceAnnotationId{7},
          .range = SourceRange{.source = SourceId{2}, .offset = 0, .size = 1},
          .label = "Colliding",
      }}},
  };

  bool threw = false;
  try {
    state.appendScan(SourceId{2}, std::move(result));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "session state should reject source annotation ids owned by an earlier scan");
  expect(state.assets().empty(), "source annotation collision should be rejected before scan assets are published");
  expect(state.sourceMap().annotations().size() == 1,
         "source annotation collision should not mutate existing session state");
}

void sessionStateScrubsCrossSourceObjectLinks() {
  SessionState state;
  const SourceId firstSource{1};
  const SourceId secondSource{2};
  state.appendScan(firstSource,
                   ScanResult{
                       .assets = {MiscAsset{.metadata =
                                                AssetMetadata{
                                                    .id = AssetId{1},
                                                    .range = SourceRange{.source = firstSource, .offset = 0, .size = 1},
                                                }}},
                       .sourceMap = SourceMap{{SourceAnnotation{
                           .id = SourceAnnotationId{1},
                           .range = SourceRange{.source = firstSource, .offset = 0, .size = 1},
                           .owner = ObjectRefs::misc(AssetId{1}),
                       }}},
                   });
  state.appendScan(
      secondSource,
      ScanResult{
          .assets = {MiscAsset{.metadata =
                                   AssetMetadata{
                                       .id = AssetId{2},
                                       .range = SourceRange{.source = secondSource, .offset = 0, .size = 1},
                                   }}},
          .sourceMap = SourceMap{{SourceAnnotation{
              .id = SourceAnnotationId{2},
              .range = SourceRange{.source = secondSource, .offset = 0, .size = 1},
              .owner = ObjectRefs::misc(AssetId{2}),
              .links = {SourceLink{
                  .role = SourceLinkRole::Related,
                  .target = ObjectRefs::misc(AssetId{1}),
              }},
          }}},
      });

  const std::array removedSources{firstSource};
  state.removeSources(removedSources);

  expect(!state.containsAsset(AssetId{1}) && state.containsAsset(AssetId{2}),
         "source removal should retain assets owned by surviving sources");
  const SourceMap remaining = state.sourceMap();
  expect(remaining.annotations().size() == 1 && remaining.annotations().front().id == SourceAnnotationId{2} &&
             remaining.annotations().front().links.empty(),
         "source removal should scrub surviving links to removed assets");
}

void scanValidationRejectsSourceAnnotationParentCycles() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "cycle.bin"}, {0, 0});
  ScanResult result{
      .sourceMap = SourceMap{{
          SourceAnnotation{
              .id = SourceAnnotationId{1},
              .range = SourceRange{.source = source, .offset = 0, .size = 1},
              .label = "First",
              .parent = SourceAnnotationId{2},
          },
          SourceAnnotation{
              .id = SourceAnnotationId{2},
              .range = SourceRange{.source = source, .offset = 1, .size = 1},
              .label = "Second",
              .parent = SourceAnnotationId{1},
          },
      }},
  };
  const ValidationReport report = validateScanResult(source, result, sources, {});
  expect(std::ranges::any_of(
             report.diagnostics(),
             [](const Diagnostic& diagnostic) { return diagnostic.code == "scan.source-annotation.parent-cycle"; }),
         "scan admission should reject cyclic annotation parents before a TreeView can recurse through them");
}

void scanValidationRequiresAssetOwnedAnnotationGraphs() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "unowned.bin"}, {0});
  const SourceRange range = sources.reader(source).range(0, 1);
  ScanResult result{
      .assets = {MiscAsset{.metadata = AssetMetadata{.id = AssetId{1}, .name = "Unowned", .range = range}}},
      .sourceMap = SourceMap{{SourceAnnotation{
          .id = SourceAnnotationId{1},
          .range = range,
          .label = "Unowned",
      }}},
  };

  const ValidationReport report = validateScanResult(source, result, sources, {});
  expect(std::ranges::any_of(
             report.diagnostics(),
             [](const Diagnostic& diagnostic) { return diagnostic.code == "scan.asset.missing-source-annotations"; }),
         "scan admission should require every asset to expose an explicitly owned annotation graph");
}

void scanValidationRejectsCrossAssetAnnotationParents() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "cross-owned.bin"}, {0, 0});
  const SourceRange range = sources.reader(source).range(0, 2);
  ScanResult result{
      .assets =
          {
              MiscAsset{.metadata = AssetMetadata{.id = AssetId{1}, .name = "First", .range = range}},
              MiscAsset{.metadata = AssetMetadata{.id = AssetId{2}, .name = "Second", .range = range}},
          },
      .sourceMap = SourceMap{{
          SourceAnnotation{
              .id = SourceAnnotationId{1},
              .range = sources.reader(source).range(0, 1),
              .label = "First",
              .owner = ObjectRefs::misc(AssetId{1}),
          },
          SourceAnnotation{
              .id = SourceAnnotationId{2},
              .range = sources.reader(source).range(1, 1),
              .label = "Second",
              .owner = ObjectRefs::misc(AssetId{2}),
              .parent = SourceAnnotationId{1},
          },
      }},
  };

  const ValidationReport report = validateScanResult(source, result, sources, {});
  expect(std::ranges::any_of(report.diagnostics(),
                             [](const Diagnostic& diagnostic) {
                               return diagnostic.code == "scan.source-annotation.cross-asset-parent";
                             }),
         "scan admission should reject annotation parents that cross asset boundaries");
}

void objectSelectorsHaveDistinctKinds() {
  expect(ObjectRefs::instrumentIndex(2) != ObjectRefs::instrumentProgram(2, 0),
         "an instrument table index should not collide with a bank/program selector");
  expect(ObjectRefs::sampleIndex(3) != ObjectRefs::sample(AssetId{0}, 3),
         "an unresolved sample index should not collide with a concrete sample object");
}

void diagnosticsCanReferenceSourceAnnotationsAndObjects() {
  ScanIdAllocator ids;
  SourceMapBuilder builder([&ids]() { return ids.nextSourceAnnotationId(); });
  const SourceId source{4};
  const AssetId asset{9};
  const SourceRange headerRange{.source = source, .offset = 2, .size = 3};
  const auto header = builder.header("Probe Header", headerRange).owner(ObjectRefs::asset(asset));
  const SourceMap sourceMap = builder.finish();

  const Diagnostic annotationDiagnostic{
      .severity = Severity::Warning,
      .message = "annotation diagnostic",
      .range = headerRange,
      .annotation = header.id(),
  };
  const Diagnostic objectDiagnostic{
      .severity = Severity::Error,
      .message = "object diagnostic",
      .object = ObjectRefs::asset(asset),
  };

  expect(sourceMap.find(*annotationDiagnostic.annotation) != nullptr,
         "diagnostics should be able to anchor to source annotations");
  expect(objectDiagnostic.object &&
             sourceMap.ownedBy(*objectDiagnostic.object) == std::vector<SourceAnnotationId>{header.id()},
         "diagnostics should be able to anchor to semantic objects");
}

void sessionSnapshotCarriesScannerSourceMap() {
  Session session;
  session.registerFormat(probeExplicitCollectionModule(), probeSequenceDialect());

  const auto source = session.addSource(SourceFile{.name = "annotated.probe"}, {0xab, 0x01, 0x02});
  session.scanSource(source);
  SessionSnapshot snapshot = session.snapshot();

  const auto headerIds = snapshot.sourceMap().withRole(source, SourceRole::Header);
  expect(headerIds.size() == 1, "session snapshot should publish scanner source annotations");
  const auto& header = snapshot.sourceMap().get(headerIds[0]);
  expect(header.label == "Probe Header", "session source map should preserve annotation labels");
  expect(header.owner && header.owner->kind == ObjectKind::Sequence && snapshot.collections()[0].sequence &&
             header.owner->asset == *snapshot.collections()[0].sequence,
         "session source map should preserve annotation ownership");
  expect(header.fields.size() == 1 && std::get<u64>(header.fields[0].value) == 0xab,
         "session source map should preserve annotation fields");

  const auto inspection = session.inspect(header.owner->asset);
  expect(inspection && inspection->bytes().size() == 3 && inspection->bytes().front() == 0xab,
         "source inspection should expand a primary header range across its explicitly owned graph");
  expect(inspection->roots().size() == 2 && inspection->annotation(inspection->roots().front()) != nullptr,
         "source inspection should preserve the asset-owned annotation graph");

  session.removeSource(source);

  snapshot = session.snapshot();
  expect(snapshot.sourceMap().empty(), "removing a source should remove its source annotations");
  expect(inspection->bytes().size() == 3 && inspection->bytes().front() == 0xab,
         "an existing source inspection should survive source removal");
}

}  // namespace

void runValueSourceMapTests() {
  sourceMapBuilderRecordsAnnotationsFieldsAndLinks();
  sourceAnnotationsCarryOutlinePolicyForTreeConsumers();
  sourceMapRejectsDuplicateAnnotationIds();
  sessionStateRejectsCrossScanAnnotationIdCollisions();
  sessionStatePreflightsSourceAnnotationIdCollisions();
  sessionStateScrubsCrossSourceObjectLinks();
  scanValidationRejectsSourceAnnotationParentCycles();
  scanValidationRequiresAssetOwnedAnnotationGraphs();
  scanValidationRejectsCrossAssetAnnotationParents();
  objectSelectorsHaveDistinctKinds();
  diagnosticsCanReferenceSourceAnnotationsAndObjects();
  sessionSnapshotCarriesScannerSourceMap();
}

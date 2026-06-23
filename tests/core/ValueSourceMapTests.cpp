/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

namespace {

void sourceMapBuilderRecordsAnnotationsFieldsAndLinks() {
  ScanIdAllocator ids;
  SourceMapBuilder builder([&ids]() { return ids.nextSourceAnnotationId(); });
  const SourceId source{3};
  const SourceRange headerRange{.source = source, .offset = 0, .size = 4};
  const SourceRange tableRange{.source = source, .offset = 8, .size = 8};

  const auto header = builder.header("Probe Header", headerRange)
                          .field("Magic", SourceRange{.source = source, .offset = 0, .size = 1}, u8{0xaa},
                                 SourceValueDisplay::Hex)
                          .derived("Decoded", "yes")
                          .link(SourceLinkRole::PointsTo, tableRange, "Table");
  const auto table = builder.table("Pointer Table", tableRange).parent(header.id());
  const auto command = builder.command("Pitch Bend Range", SourceRange{.source = source, .offset = 10, .size = 2},
                                       SequenceSemantic::Pitch)
                            .field("bend", SourceRange{.source = source, .offset = 10, .size = 1}, s8{-2},
                                   SourceValueDisplay::SignedDecimal);

  const SourceMap sourceMap = builder.finish();
  expect(sourceMap.annotations().size() == 3, "source map should contain all builder annotations");

  const auto& headerAnnotation = sourceMap.get(header.id());
  expect(headerAnnotation.role == SourceRole::Header, "header helper should create a header annotation");
  expect(headerAnnotation.localKind == "probe-header", "source map should slugify annotation labels");
  expect(headerAnnotation.fields.size() == 2, "source map should preserve direct and derived fields");
  expect(std::get<u64>(headerAnnotation.fields[0].value) == 0xaa,
         "source map should preserve field values");
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
  expect(sourceMap.get(command.id()).fields.size() == 1 &&
             sourceMap.get(command.id()).fields[0].name == "bend" &&
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
  expect(sourceMap.withSequenceSemantic(source, SequenceSemantic::Pitch) ==
             std::vector<SourceAnnotationId>{command.id()},
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
  const auto hiddenHeader =
      builder.header("Internal Header", SourceRange{.source = source, .offset = 2, .size = 1})
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
  expect(objectDiagnostic.object && sourceMap.ownedBy(*objectDiagnostic.object) == std::vector<SourceAnnotationId>{header.id()},
         "diagnostics should be able to anchor to semantic objects");
}

void sessionSnapshotCarriesScannerSourceMap() {
  Session session;
  session.formats().add(probeExplicitCollectionModule());
  session.dialects().add(probeSequenceDialect());

  const auto source = session.addSource(SourceFile{.name = "annotated.probe"}, {0xab});
  SessionSnapshot snapshot = session.scanSource(source);

  const auto headerIds = snapshot.sourceMap().withRole(source, SourceRole::Header);
  expect(headerIds.size() == 1, "session snapshot should publish scanner source annotations");
  const auto& header = snapshot.sourceMap().get(headerIds[0]);
  expect(header.label == "Probe Header", "session source map should preserve annotation labels");
  expect(header.owner && header.owner->kind == ObjectKind::Sequence && snapshot.collections()[0].sequence &&
             header.owner->asset == *snapshot.collections()[0].sequence,
         "session source map should preserve annotation ownership");
  expect(header.fields.size() == 1 && std::get<u64>(header.fields[0].value) == 0xab,
         "session source map should preserve annotation fields");

  snapshot = session.removeSource(source);
  expect(snapshot.sourceMap().empty(), "removing a source should remove its source annotations");
}

}  // namespace

void runValueSourceMapTests() {
  sourceMapBuilderRecordsAnnotationsFieldsAndLinks();
  sourceAnnotationsCarryOutlinePolicyForTreeConsumers();
  sourceMapRejectsDuplicateAnnotationIds();
  diagnosticsCanReferenceSourceAnnotationsAndObjects();
  sessionSnapshotCarriesScannerSourceMap();
}

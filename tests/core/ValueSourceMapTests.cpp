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
                                       SequenceSemantic::Pitch);

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

  expect(sourceMap.annotationsForSource(source).size() == 3, "source map should index annotations by source");
  expect(sourceMap.at(source, 10) == std::vector<SourceAnnotationId>{table.id(), command.id()},
         "source map should find annotations at a byte offset");
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
  sourceMapRejectsDuplicateAnnotationIds();
  sessionSnapshotCarriesScannerSourceMap();
}

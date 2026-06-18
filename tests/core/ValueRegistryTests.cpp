/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceCursorDialect.h"
#include "value/sequence/bytecode/BytecodeWalkers.h"

namespace {

struct ProbeMetaCommand : NoOpCommand, NoOperands<ProbeMetaCommand> {
  static constexpr CommandMeta meta = commandMeta("local-meta", "Local Meta");
};

struct CursorProbeState {
  u8 transpose = 1;
};

struct CursorProbeContext {
  double velocity = 0.5;
};

struct CursorProbeReader {
  template <class Runtime>
  static CommandFlow read(Runtime& rt, VmCommandCursor& cmd) {
    switch (cmd.opcode()) {
      case 0x90: {
        cmd.name("Note").semantic(SequenceSemantic::Note);
        const u8 key = cmd.u8("key");
        const u8 duration = cmd.u8("duration");
        rt.note(key + rt.state.transpose, rt.context.velocity, duration);
        return cmd.wait(duration);
      }
      case 0xff:
        return cmd.name("End")
            .kind("end")
            .semantic(SequenceSemantic::End)
            .playbackStatus(CommandPlaybackStatus::StopsPlayback)
            .end();
      default:
        return cmd.name("Unsupported Opcode")
            .kind("unsupported")
            .semantic(SequenceSemantic::Unsupported)
            .unsupported("Unsupported cursor probe opcode")
            .end();
    }
  }
};

void bytecodeMapRejectsIncompatibleHandlerReuse() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x10, ProbeProgramCommand>(commandMeta("shared", "Shared"));

  bool threw = false;
  try {
    map.op<0x11, ProbeNoteCommand>(commandMeta("shared", "Shared"));
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "bytecode map should reject one kind reused for different command types");
}

void bytecodeMapRejectsOpcodeRangeOverlap() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x12, ProbeProgramCommand>("Program");
  map.range<0x10, 0x20, ProbeNoteCommand>("Note");
  map.unknown<ProbeEndCommand>("End");

  bool threw = false;
  try {
    static_cast<void>(map.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "bytecode map should reject overlapping exact opcode and range declarations");
}

void bytecodeMapRequiresFallbackCommand() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x10, ProbeProgramCommand>("Program");

  bool threw = false;
  try {
    static_cast<void>(map.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "bytecode map should require an unknown or truncated fallback command");
}

void bytecodeMapUsesCommandLocalMetadata() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x20, ProbeMetaCommand>();
  map.unknown<ProbeEndCommand>("End");

  const BytecodeDispatchTable table = map.finish();
  const SequenceDialect dialect = builder.finish();
  const auto& spec = table.opcodes[0x20];
  expect(spec.has_value() && spec->kindName == "probe-bytecode.local-meta" && spec->name == "Local Meta",
         "bytecode map should use command-local metadata when no display name is passed");
  const auto* commandKind = dialect.kind(spec->kind);
  expect(commandKind != nullptr && commandKind->kindName == "probe-bytecode.local-meta" &&
             commandKind->name == "Local Meta",
         "command-local metadata should register the matching dialect kind");
  expect(dialect.handler(spec->handler) != nullptr, "command-local metadata should register an executable handler");
}

void bytecodeMapAllowsOneHandlerForSeveralKinds() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x20, ProbeMetaCommand>(commandMeta("meta-a", "Meta A"));
  map.op<0x21, ProbeMetaCommand>(commandMeta("meta-b", "Meta B"));
  map.unknown<ProbeEndCommand>("End");

  const BytecodeDispatchTable registeredTable = map.finish();
  const SequenceDialect dialect = builder.finish();
  expect(dialect.handlers.size() == 2 && dialect.kinds.size() == 3,
         "one command type should register one handler but keep distinct source kinds");
  expect(registeredTable.opcodes[0x20]->handler == registeredTable.opcodes[0x21]->handler &&
             registeredTable.opcodes[0x20]->kind != registeredTable.opcodes[0x21]->kind,
         "two opcodes can share execution while keeping separate source identities");

  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> decodeMap{"probe-bytecode", dialect};
  decodeMap.op<0x20, ProbeMetaCommand>(commandMeta("meta-a", "Meta A"));
  decodeMap.op<0x21, ProbeMetaCommand>(commandMeta("meta-b", "Meta B"));
  decodeMap.unknown<ProbeEndCommand>("End");
  const BytecodeDispatchTable decodeTable = decodeMap.finish();
  expect(decodeTable.opcodes[0x20]->handler == registeredTable.opcodes[0x20]->handler &&
             decodeTable.opcodes[0x21]->kind == registeredTable.opcodes[0x21]->kind,
         "decode-time map construction should reuse the registered handler and kind IDs");
}

void cursorDialectDecodesAnnotationsAndRendersThroughVm() {
  const SequenceDialect dialect =
      makeCursorDialect<CursorProbeState, CursorProbeContext, CursorProbeReader>(CursorDialectSpec<CursorProbeContext>{
          .id = "cursor-probe",
          .commandKindPrefix = "cursor-probe",
          .timebase = Timebase{.ppqn = 48},
          .context = CursorProbeContext{.velocity = 0.25},
      });
  expect(dialect.handlers.size() == 1 && dialect.kinds.empty(),
         "cursor dialect should register one generic handler and no opcode-specific kinds");

  const std::vector<u8> bytes{0x90, 60, 12, 0xff};
  const ByteReader reader(SourceId{0}, bytes);
  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> diagnostics;
  auto track = decodeLinearBytecodeTrack(reader, 0, 0, LinearBytecodeDecodePolicy{}, [&](u32 offset) {
    return decodeCursorCommand<CursorProbeState, CursorProbeContext, CursorProbeReader>(
        reader, offset, dialect,
        BytecodeDecodeContext{
            .bytecodeEnd = static_cast<u32>(bytes.size()),
            .sourceMap = &sourceMap,
            .diagnostics = &diagnostics,
        });
  });

  expect(track.commands.size() == 2, "cursor-backed decode should produce note and end commands");
  expect(track.commandKinds.size() == 2 && track.commandKinds[0].kindName == "cursor-probe.note" &&
             track.commandKinds[1].kindName == "cursor-probe.end",
         "cursor-backed decode should store source command kinds on the parsed track");
  const SourceMap annotations = sourceMap.finish();
  const auto commandAnnotations = annotations.withRole(SourceId{0}, SourceRole::Command);
  expect(commandAnnotations.size() == 2, "cursor-backed decode should record source command annotations");
  const auto& noteAnnotation = annotations.get(commandAnnotations[0]);
  expect(noteAnnotation.label == "Note" && noteAnnotation.localKind == "note" && noteAnnotation.range.offset == 0 &&
             noteAnnotation.range.size == 3,
         "cursor-backed note annotation should use the final decoded command range");
  expect(track.commands[0].annotation == noteAnnotation.id,
         "cursor-backed source commands should retain their primary source annotation");
  expect(noteAnnotation.fields.size() == 3 && noteAnnotation.fields[1].name == "key" &&
             std::get<u64>(noteAnnotation.fields[1].value) == 60,
         "cursor-backed decode should record opcode and operand fields");
  expect(diagnostics.empty(), "valid cursor-backed decode should not emit diagnostics");

  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .behavior = dialect.defaultBehavior,
  };
  program.tracks.push_back(std::move(track));
  const PerformanceSequence performance = SequenceVm{}.render(program, dialect);
  expect(performance.tracks.size() == 1 && performance.tracks[0].events.size() == 1,
         "cursor-backed command should render through the existing VM");
  const auto* note = std::get_if<NotePerformanceEvent>(&performance.tracks[0].events[0]);
  expect(note != nullptr && note->key == 61.0 && note->linearVelocity == 0.25 && note->durationTicks == 12,
         "cursor-backed render should rerun saved bytes with real runtime state");
  expect(note != nullptr && note->header.sourceAnnotation == program.tracks[0].commands[0].annotation,
         "cursor-backed performance events should retain source annotation origin");
}

void formatRegistryStoresCopyableModuleValues() {
  FormatRegistry registry;
  registry.add(probeSequenceModule());
  registry.add(FormatModule{
      .name = std::string("DynamicProbe"),
      .canScan = canScanProbeSequence,
      .scan = scanProbeSequence,
  });

  const FormatRegistry copy = registry;
  const std::array<u8, 1> probeBytes{0xaa};
  expect(copy.modules().size() == 2, "format registry should copy registered module values");
  expect(copy.modules()[0].name == "ProbeSequence", "format registry should preserve copied module names");
  expect(copy.modules()[1].name == "DynamicProbe", "format registry should own dynamically registered module names");
  expect(copy.modules()[0].canScan(SourceFile{}, probeBytes),
         "format registry should preserve copied module scan predicates");

  bool threw = false;
  try {
    registry.add(FormatModule{
        .name = "Broken",
    });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "format registry should reject incomplete module values");
}

void sequenceDialectRegistryStoresCopyableDialectValues() {
  SequenceDialectRegistry registry;
  registry.add(probeSequenceDialect());

  const SequenceDialectRegistry copy = registry;
  const auto* dialect = copy.find("probe");
  expect(dialect != nullptr, "sequence dialect registry should copy registered dialect values");
  expect(dialect->kindForName(ProbeNoteCommand::kind) != nullptr,
         "sequence dialect registry should preserve copied command kinds");
  expect(copy.find("Missing") == nullptr, "sequence dialect registry should return null for a missing dialect");
  expect(copy.contains("probe"), "sequence dialect registry should report copied dialect keys");

  bool threw = false;
  try {
    registry.add(SequenceDialect{});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "sequence dialect registry should reject dialects with empty IDs");
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

  const auto sequence = out.sequence("Builder Sequence", wholeSource)
                            .program(SequenceProgram{
                                .dialect = DialectId{.value = "probe"},
                                .timebase = Timebase{.ppqn = 48},
                            });
  const auto bank = out.instrumentSet("Builder Bank", input.reader.range(0, 1)).instruments({});

  SampleCollection sampleCollection;
  sampleCollection.samples.push_back(Sample{
      .name = "Builder Sample",
      .codec = AudioCodec::PcmS8,
      .encodedData = input.reader.range(1, 2),
      .sampleRate = 32000,
      .channels = 1,
      .bitsPerSample = 8,
  });
  const auto samples =
      out.sampleCollection("Builder Samples", input.reader.range(1, 2)).samples(std::move(sampleCollection));

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
  expect(result.explicitCollections[0].sequence == sequence.id,
         "scan result builder should preserve the collection sequence");
  expect(result.explicitCollections[0].instrumentSets == std::vector<AssetId>{bank.id},
         "scan result builder should preserve the collection instrument set");
  expect(result.explicitCollections[0].sampleCollections == std::vector<AssetId>{samples.id},
         "scan result builder should preserve the collection sample collection");
  expect(result.diagnostics.size() == 1 && result.diagnostics[0].message == "builder warning",
         "scan result builder should preserve diagnostics");
}

void scanResultBuilderRejectsReferencedUncommittedHandles() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-uncommitted.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto sequence = out.reserveSequence();
  out.collection("Broken").sequence(sequence);

  bool threw = false;
  try {
    static_cast<void>(out.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject referenced handles that were never added");
}

void scanResultBuilderRejectsWrongRoleHandleReuse() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-wrong-role.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto sequence = out.reserveSequence();

  bool threw = false;
  try {
    out.collection("Broken").instrumentSet(ScanInstrumentSetRef{.id = sequence.id});
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject using one handle id with the wrong role");
}

void scanResultBuilderRejectsUncommittedSampleRefs() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "builder-sample-ref.probe"}, {0xaa});
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };

  ScanResultBuilder out(input, "ProbeBuilder");
  const auto samples = out.reserveSampleCollection();
  const SampleRef ref = out.sampleRef(samples, 7);
  expect(ref.collection == samples.id && ref.index == 7, "scan result builder should create typed sample refs");

  bool threw = false;
  try {
    static_cast<void>(out.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "scan result builder should reject sample refs to collections that were never added");
}

void scanResultBuilderCursorReportsMalformedFields() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "cursor.probe"}, {0xaa, 0xbb, 0xcc});
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };

  ScanResultBuilder out(input, "ProbeBuilder");
  auto cursor = out.cursor(input.reader.range(2, 1));
  expect(!cursor.le32(0, "probe field"), "parse cursor should reject fields outside its range");

  const ScanResult result = out.finish();
  expect(result.diagnostics.size() == 1, "parse cursor should report malformed fields as diagnostics");
  expect(result.diagnostics[0].message == "Could not read probe field: field is outside the parser range",
         "parse cursor diagnostic should name the failed field");
}

}  // namespace

void runValueRegistryTests() {
  bytecodeMapRejectsIncompatibleHandlerReuse();
  bytecodeMapRejectsOpcodeRangeOverlap();
  bytecodeMapRequiresFallbackCommand();
  bytecodeMapUsesCommandLocalMetadata();
  bytecodeMapAllowsOneHandlerForSeveralKinds();
  cursorDialectDecodesAnnotationsAndRendersThroughVm();
  formatRegistryStoresCopyableModuleValues();
  sequenceDialectRegistryStoresCopyableDialectValues();
  scanResultBuilderCoversCommonScannerPlumbing();
  scanResultBuilderRejectsReferencedUncommittedHandles();
  scanResultBuilderRejectsWrongRoleHandleReuse();
  scanResultBuilderRejectsUncommittedSampleRefs();
  scanResultBuilderCursorReportsMalformedFields();
}

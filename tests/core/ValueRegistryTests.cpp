/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/scan/FormatDefinition.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceCursorDialect.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/session/Session.h"

namespace {

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
      case 0x70: {
        cmd.name("Transpose", SequenceSemantic::State);
        rt.state.transpose = cmd.s8("semitones");
        return cmd.next();
      }
      case 0x90: {
        cmd.name("Note", SequenceSemantic::Note);
        const u8 key = cmd.u8("key");
        const u8 duration = cmd.u8("duration");
        cmd.derived("performed_key", key + rt.state.transpose, SourceValueDisplay::MidiNote);
        rt.note(key + rt.state.transpose, rt.context.velocity, duration);
        return cmd.wait(duration);
      }
      case 0x91: {
        cmd.name("Late Truncated Note", SequenceSemantic::Note);
        const u8 key = cmd.u8("key");
        rt.note(key + rt.state.transpose, rt.context.velocity, 12);
        static_cast<void>(cmd.u8("missing"));
        return cmd.next();
      }
      case 0xff:
        return cmd.name("End").end();
      default:
        return cmd.name("Unsupported Opcode", SequenceSemantic::Unsupported)
            .kind("unsupported")
            .unsupported("Unsupported cursor probe opcode")
            .end();
    }
  }
};

struct RepeatBreakProbeVm {
  bool called = false;

  BranchResult countedRepeatBreak(u8, Address) {
    called = true;
    return BranchResult{
        .taken = true,
        .effects = Effects{.step = Step::jump(Address{4}, JumpSemantics::FiniteBranch)},
    };
  }
};

struct RepeatUntilProbeVm {
  bool called = false;
  Effects effects;

  Effects countedRepeatUntil(u8, u32, Address) {
    called = true;
    return effects;
  }
};

void cursorDialectDecodesAnnotationsAndRendersThroughVm() {
  const SequenceDialect dialect =
      makeCursorDialect<CursorProbeState, CursorProbeContext, CursorProbeReader>(CursorDialectSpec<CursorProbeContext>{
          .id = "cursor-probe",
          .commandDetailKindPrefix = "cursor-probe",
          .timebase = Timebase{.ppqn = 48},
          .context = CursorProbeContext{.velocity = 0.25},
      });
  expect(dialect.execute != nullptr, "cursor dialect should register one generic executor");

  const std::vector<u8> bytes{0x70, 2, 0x90, 60, 12, 0xff};
  const ByteReader reader(SourceId{0}, bytes);
  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> diagnostics;
  const BytecodeDecodeContext decodeContext{
      .bytecodeEnd = static_cast<u32>(bytes.size()),
      .sourceMap = &sourceMap,
      .diagnostics = &diagnostics,
  };
  auto decodeState = makeDecodeCursorState<CursorProbeState, CursorProbeContext>(
      decodeContext, cursorContext<CursorProbeContext>(dialect));
  auto track = decodeLinearBytecodeTrack(reader, 0, 0, LinearBytecodeDecodePolicy{}, [&](u32 offset) {
    return decodeCursorCommandWithState<CursorProbeState, CursorProbeContext, CursorProbeReader>(
        reader, offset, dialect, decodeState, decodeContext);
  });

  expect(track.commands.size() == 3, "cursor-backed decode should produce state, note, and end commands");
  const SourceMap annotations = sourceMap.finish();
  const auto commandAnnotations = annotations.withRole(SourceId{0}, SourceRole::Command);
  expect(commandAnnotations.size() == 3, "cursor-backed decode should record source command annotations");
  expect(annotations.get(commandAnnotations[0]).detailKind == "cursor-probe.transpose" &&
             annotations.get(commandAnnotations[1]).detailKind == "cursor-probe.note" &&
             annotations.get(commandAnnotations[2]).detailKind == "cursor-probe.end",
         "cursor-backed decode should store command detail metadata in source annotations");
  const auto& noteAnnotation = annotations.get(commandAnnotations[1]);
  expect(noteAnnotation.label == "Note" && noteAnnotation.localKind == "note" && noteAnnotation.range.offset == 2 &&
             noteAnnotation.range.size == 3,
         "cursor-backed note annotation should use the final decoded command range");
  expect(track.commands[1].annotation == noteAnnotation.id,
         "cursor-backed source commands should retain their primary source annotation");
  expect(noteAnnotation.fields.size() == 4 && noteAnnotation.fields[1].name == "key" &&
             std::get<u64>(noteAnnotation.fields[1].value) == 60 && noteAnnotation.fields[3].name == "performed_key" &&
             std::get<s64>(noteAnnotation.fields[3].value) == 62,
         "cursor-backed decode should record opcode, operands, and state-derived fields");
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
  expect(note != nullptr && note->key == 62.0 && note->linearVelocity == 0.25 && note->durationTicks == 12,
         "cursor-backed render should rerun saved bytes with real runtime state");
  expect(note != nullptr && note->header.sourceAnnotation == program.tracks[0].commands[1].annotation,
         "cursor-backed performance events should retain source annotation origin");
}

void cursorDialectReportsWarningsOnFinalCommandRange() {
  const SequenceDialect dialect =
      makeCursorDialect<CursorProbeState, CursorProbeContext, CursorProbeReader>(CursorDialectSpec<CursorProbeContext>{
          .id = "cursor-probe",
          .commandDetailKindPrefix = "cursor-probe",
          .timebase = Timebase{.ppqn = 48},
          .context = CursorProbeContext{.velocity = 0.25},
      });

  const std::vector<u8> bytes{0xfe, 0xff};
  const ByteReader reader(SourceId{0}, bytes);
  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> diagnostics;
  const BytecodeDecodeContext decodeContext{
      .bytecodeEnd = static_cast<u32>(bytes.size()),
      .sourceMap = &sourceMap,
      .diagnostics = &diagnostics,
  };
  auto decodeState = makeDecodeCursorState<CursorProbeState, CursorProbeContext>(
      decodeContext, cursorContext<CursorProbeContext>(dialect));
  auto track = decodeLinearBytecodeTrack(reader, 0, 0, LinearBytecodeDecodePolicy{}, [&](u32 offset) {
    return decodeCursorCommandWithState<CursorProbeState, CursorProbeContext, CursorProbeReader>(
        reader, offset, dialect, decodeState, decodeContext);
  });

  expect(track.commands.size() == 1, "unsupported cursor command should stop linear decode");
  expect(diagnostics.size() == 1 && diagnostics[0].message == "Unsupported cursor probe opcode",
         "unsupported cursor command should report one warning");
  expect(diagnostics[0].range && sameRange(*diagnostics[0].range, SourceRange{SourceId{0}, 0, 1}),
         "cursor warnings should use the final decoded command range");
  expect(diagnostics[0].annotation && *diagnostics[0].annotation == track.commands[0].annotation,
         "cursor warnings should attach to the decoded source annotation");
  const SourceMap annotations = sourceMap.finish();
  expect(annotations.get(track.commands[0].annotation).playbackStatus == CommandPlaybackStatus::Unsupported,
         "unsupported cursor command should persist unsupported playback status");
}

void cursorFlowHelpersInferMetadata() {
  const std::array<u8, 4> bytes{0x94, 0x12, 0x34, 0x56};
  VmCommandCursor jump(CommandPhase::Decode, probeRange(0, bytes.size()), bytes);
  static_cast<void>(jump.name("Jump").jump(Address{0x1234}));
  const CursorCommandMetadata jumpMetadata = jump.metadata("cursor-probe");
  expect(jumpMetadata.semantic == SequenceSemantic::Jump &&
             jumpMetadata.playbackStatus == CommandPlaybackStatus::AffectsControlFlow,
         "jump flow should infer jump semantic and control-flow status");

  VmCommandCursor end(CommandPhase::Decode, probeRange(0, bytes.size()), bytes);
  static_cast<void>(end.name("End").end());
  const CursorCommandMetadata endMetadata = end.metadata("cursor-probe");
  expect(endMetadata.semantic == SequenceSemantic::End &&
             endMetadata.playbackStatus == CommandPlaybackStatus::StopsPlayback,
         "end flow should infer end semantic and stops-playback status");

  VmCommandCursor explicitCommand(CommandPhase::Decode, probeRange(0, bytes.size()), bytes);
  static_cast<void>(
      explicitCommand.name("Display Only", SequenceSemantic::Meta, CommandPlaybackStatus::SourceOnly).jump(Address{4}));
  const CursorCommandMetadata explicitMetadata = explicitCommand.metadata("cursor-probe");
  expect(explicitMetadata.semantic == SequenceSemantic::Meta &&
             explicitMetadata.playbackStatus == CommandPlaybackStatus::SourceOnly,
         "flow helper defaults should not override explicit command metadata");
}

void cursorPreserveRecordsMetadataAndBytes() {
  const std::array<u8, 3> bytes{0xe0, 0x12, 0x34};
  SourceMapBuilder sourceMap;
  VmCommandCursor cursor(CommandPhase::Decode, probeRange(10, bytes.size()), bytes, &sourceMap);

  const CommandFlow flow = cursor.preserve("Ignored Command", 2, "ignored-command");
  const CursorCommandMetadata metadata = cursor.metadata("cursor-probe");
  const SourceMap annotations = sourceMap.finish();
  const SourceAnnotation& annotation = annotations.get(cursor.annotation());

  expect(flow.kind == FlowKind::Next, "preserved cursor command should continue to the next command");
  expect(metadata.name == "Ignored Command" && metadata.detailKind == "cursor-probe.ignored-command" &&
             metadata.semantic == SequenceSemantic::Meta &&
             metadata.playbackStatus == CommandPlaybackStatus::SourceOnly,
         "preserved cursor command should use source-only meta metadata");
  expect(annotation.playbackStatus == CommandPlaybackStatus::SourceOnly,
         "preserved cursor command should persist source-only playback status");
  const auto bytesField =
      std::ranges::find_if(annotation.fields, [](const SourceField& field) { return field.name == "bytes"; });
  expect(bytesField != annotation.fields.end() && std::get<std::string>(bytesField->value) == "12 34",
         "preserved cursor command should record raw operand bytes in source annotations");
  expect(sameRange(bytesField->range, SourceRange{.source = SourceId{0}, .offset = 11, .size = 2}),
         "preserved cursor command annotation should preserve the raw-byte operand range");
}

void cursorRepeatBreakDoesNotMutateVmAfterTruncatedRead() {
  const std::array<u8, 1> bytes{0x12};
  VmCommandCursor cursor(CommandPhase::Render, probeRange(0, bytes.size()), bytes);
  static_cast<void>(cursor.u8("missing"));

  RepeatBreakProbeVm vm;
  const RepeatBreakFlow flow = detail::resolveRenderCursorRepeatBreak(cursor, vm, 0, Address{4});
  expect(!vm.called, "truncated cursor repeat-break should not ask the VM to mutate repeat state");
  expect(!flow.taken() && flow.flow().truncated, "truncated cursor repeat-break should remain an untaken truncation");
}

void cursorRepeatUntilReportsFallthroughAndSkipsTruncatedVmMutation() {
  const std::array<u8, 1> bytes{0x12};

  VmCommandCursor fallthroughCursor(CommandPhase::Render, probeRange(0, bytes.size()), bytes);
  RepeatUntilProbeVm fallthroughVm{.effects = Effects::none()};
  const RepeatUntilFlow fallthrough =
      detail::resolveRenderCursorRepeatUntil(fallthroughCursor, fallthroughVm, 0, 3, Address{4});
  expect(fallthroughVm.called, "render cursor repeat-until should ask VM to resolve repeat state");
  expect(fallthrough.fallsThrough(), "render cursor repeat-until should report VM fallthrough");

  VmCommandCursor branchCursor(CommandPhase::Render, probeRange(0, bytes.size()), bytes);
  RepeatUntilProbeVm branchVm{.effects = Effects{.step = Step::jump(Address{4})}};
  const RepeatUntilFlow branch = detail::resolveRenderCursorRepeatUntil(branchCursor, branchVm, 0, 3, Address{4});
  expect(branchVm.called, "render cursor repeat-until branch should ask VM to resolve repeat state");
  expect(!branch.fallsThrough(), "render cursor repeat-until should report VM branch");

  VmCommandCursor truncatedCursor(CommandPhase::Render, probeRange(0, bytes.size()), bytes);
  static_cast<void>(truncatedCursor.u8("missing"));
  RepeatUntilProbeVm truncatedVm{.effects = Effects::none()};
  const RepeatUntilFlow truncated =
      detail::resolveRenderCursorRepeatUntil(truncatedCursor, truncatedVm, 0, 3, Address{4});
  expect(!truncatedVm.called, "truncated cursor repeat-until should not ask the VM to mutate repeat state");
  expect(!truncated.fallsThrough() && truncated.flow().truncated,
         "truncated cursor repeat-until should remain a non-fallthrough truncation");
}

void cursorDialectSuppressesMalformedRenderEvents() {
  const SequenceDialect dialect =
      makeCursorDialect<CursorProbeState, CursorProbeContext, CursorProbeReader>(CursorDialectSpec<CursorProbeContext>{
          .id = "cursor-probe",
          .commandDetailKindPrefix = "cursor-probe",
          .timebase = Timebase{.ppqn = 48},
          .context = CursorProbeContext{.velocity = 0.25},
      });

  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};
  const std::array<u8, 2> bytes{0x91, 60};
  builder.addDecoded(Address{0}, probeRange(0, bytes.size()), bytes);

  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
  };
  program.tracks.push_back(std::move(track));

  const PerformanceSequence performance = SequenceVm{}.render(program, dialect);
  expect(performance.tracks.size() == 1, "malformed cursor-backed command should still produce a performance track");
  expect(performance.tracks[0].events.empty(), "malformed cursor-backed command should not leak buffered events");
}

void formatRegistryStoresCopyableModuleValues() {
  FormatRegistry registry;
  registry.add(probeSequenceModule());
  registry.add(FormatModule{
      .name = std::string("DynamicProbe"),
      .scan = scanProbeSequence,
  });

  const FormatRegistry copy = registry;
  const std::array<u8, 1> probeBytes{0xaa};
  expect(copy.modules().size() == 2, "format registry should copy registered module values");
  expect(copy.modules()[0].name == "ProbeSequence", "format registry should preserve copied module names");
  expect(copy.modules()[1].name == "DynamicProbe", "format registry should own dynamically registered module names");
  expect(copy.modules()[0].canScan(SourceFile{}, probeBytes),
         "format registry should preserve copied module scan predicates");
  expect(copy.modules()[1].canScan == nullptr,
         "format registry should accept scan-only modules without a duplicate recognition probe");

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
  expect(dialect->execute != nullptr, "sequence dialect registry should preserve copied command executor");
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

void sessionRegistersOneFormatDefinitionAtTheAuthoringSurface() {
  Session session;
  session.registerFormat(FormatDefinition{
      .module = probeSequenceModule(),
      .sequenceDialect = probeSequenceDialect(),
  });

  expect(session.formats().modules().size() == 1 && session.formats().modules()[0].name == "ProbeSequence",
         "format definition should register its scanner");
  expect(session.dialects().contains("probe"), "format definition should register its executor family");
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
  auto validCursor = out.cursor(input.reader.range(1, 2));
  const auto value = validCursor.le16(0, "probe value");
  expect(value && *value == 0xccbb, "parse cursor should return parsed field values");
  expect(sameRange(value.range, SourceRange{.source = source, .offset = 1, .size = 2}),
         "parse cursor should return parsed field ranges");
  out.sourceMap().header("Probe Header", input.reader.range(1, 2)).field("probe_value", value);

  auto cursor = out.cursor(input.reader.range(2, 1));
  expect(!cursor.le32(0, "probe field"), "parse cursor should reject fields outside its range");

  const ScanResult result = out.finish();
  const auto headerIds = result.sourceMap.withRole(source, SourceRole::Header);
  expect(headerIds.size() == 1, "ranged parse values should be accepted by annotation fields");
  const auto& header = result.sourceMap.get(headerIds[0]);
  expect(header.fields.size() == 1 && header.fields[0].name == "probe_value" &&
             std::get<u64>(header.fields[0].value) == 0xccbb &&
             sameRange(header.fields[0].range, SourceRange{.source = source, .offset = 1, .size = 2}),
         "annotation fields should use the parsed value range");
  expect(result.diagnostics.size() == 1, "parse cursor should report malformed fields as diagnostics");
  expect(result.diagnostics[0].message == "Could not read probe field: field is outside the parser range",
         "parse cursor diagnostic should name the failed field");
}

}  // namespace

void runValueRegistryTests() {
  cursorDialectDecodesAnnotationsAndRendersThroughVm();
  cursorDialectReportsWarningsOnFinalCommandRange();
  cursorFlowHelpersInferMetadata();
  cursorPreserveRecordsMetadataAndBytes();
  cursorRepeatBreakDoesNotMutateVmAfterTruncatedRead();
  cursorRepeatUntilReportsFallthroughAndSkipsTruncatedVmMutation();
  cursorDialectSuppressesMalformedRenderEvents();
  formatRegistryStoresCopyableModuleValues();
  sequenceDialectRegistryStoresCopyableDialectValues();
  sessionRegistersOneFormatDefinitionAtTheAuthoringSurface();
  scanResultBuilderCoversCommonScannerPlumbing();
  scanResultBuilderRejectsReferencedUncommittedHandles();
  scanResultBuilderRejectsWrongRoleHandleReuse();
  scanResultBuilderRejectsUncommittedSampleRefs();
  scanResultBuilderCursorReportsMalformedFields();
}

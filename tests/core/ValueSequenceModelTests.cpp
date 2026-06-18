/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/sequence/SequenceCursor.h"

namespace {

void levelScaleRoundTripsMidiValues() {
  for (u32 value = 0; value <= 127; ++value) {
    const auto midiValue = static_cast<u8>(value);
    expect(LevelScale::midi7FromLinear(LevelScale::linearFromMidi7(midiValue)) == midiValue,
           "MIDI-shaped 7-bit levels should round-trip through linear gain");
  }

  for (u32 value = 0; value <= 16383; ++value) {
    const auto midiValue = static_cast<u16>(value);
    expect(LevelScale::midi14FromLinear(LevelScale::linearFromMidi14(midiValue)) == midiValue,
           "MIDI-shaped 14-bit levels should round-trip through linear gain");
  }
}

void byteReaderChecksBoundsAndEndian() {
  const std::vector<u8> bytes{0x00, 0x34, 0x12, 0x78, 0x56};
  const ByteReader reader(SourceId{7}, bytes);

  expect(reader.has(1, 4), "reader should report valid four-byte range");
  expect(!reader.has(4, 2), "reader should reject range past end");
  expect(reader.u8At(1) == 0x34, "reader should read u8");
  expect(reader.le16(1) == 0x1234, "reader should read little-endian u16");
  expect(reader.be16(1) == 0x3412, "reader should read big-endian u16");
  expect(reader.le32(1) == 0x56781234, "reader should read little-endian u32");
  expect(reader.be32(1) == 0x34127856, "reader should read big-endian u32");

  bool threw = false;
  try {
    static_cast<void>(reader.u8At(5));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  expect(threw, "reader should throw on out-of-range access");
}

void commandReaderRejectsUnterminatedVariableLengthOperands() {
  std::vector<CommandOperand> operands;
  const std::array<u8, 3> completeBytes{0x80, 0x81, 0x05};
  CommandReader completeReader{probeRange(0, completeBytes.size()), completeBytes, &operands};
  expect(completeReader.varLen("duration") == 133, "command reader should decode complete variable-length operands");
  expect(operands.size() == 1 && operands[0].name == "duration" && operands[0].range.offset == 1 &&
             operands[0].range.size == 2,
         "command reader should record complete variable-length operand metadata");

  bool rejectedUnterminated = false;
  const std::array<u8, 2> unterminatedBytes{0x80, 0x81};
  CommandReader unterminatedReader{probeRange(0, unterminatedBytes.size()), unterminatedBytes, &operands};
  try {
    static_cast<void>(unterminatedReader.varLen("duration"));
  } catch (const std::out_of_range&) {
    rejectedUnterminated = true;
  }
  expect(rejectedUnterminated, "command reader should reject variable-length operands without a terminating byte");
  expect(operands.size() == 1, "unterminated variable-length operands should not be recorded as decoded operands");
}

void sourceCommandsPreserveBytesOperandsAndDialectDisplay() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};
  const std::array<u8, 2> programBytes{0x80, 0x05};
  const SourceCommand& command = addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0},
                                                                      probeRange(0, programBytes.size()), programBytes);

  expect(track.commands.size() == 1, "track builder should append one source command");
  expect(track.commandBytes.size() == programBytes.size(), "track builder should pool command bytes");
  expect(track.bytesFor(command)[0] == 0x80 && track.bytesFor(command)[1] == 0x05,
         "source command should point back to its stored bytes");

  const auto operands = track.operandsFor(command);
  expect(operands.size() == 1, "source command should retain decoded operands");
  expect(operands[0].name == "program", "decoded operand should preserve its source name");
  expect(std::get<u64>(operands[0].value) == 5, "decoded operand should preserve its raw value");
  expect(operands[0].range.offset == 1 && operands[0].range.size == 1,
         "decoded operand should preserve its source range");

  const CommandInfo info = dialect.describe(track, command);
  expect(info.name == "Program", "dialect display should use the registered command name");
  expect(info.detailKind == "probe.program", "dialect display should use the registered command kind");
  expect(info.playbackStatus == CommandPlaybackStatus::AffectsPlayback,
         "dialect display should expose command playback status");
  expect(info.fields.size() == 1 && info.fields[0].name == "program" && info.fields[0].value == "5",
         "dialect display should be derived by replaying the format-local command parser");

  ItemTree itemTree;
  ScanIdAllocator ids;
  ItemTreeBuilder items(itemTree, ids);
  const ItemId root = items.add(std::nullopt, ItemKind::Track, "probe.track", "Track", probeRange(0, 0));
  const ItemId commandItem = addSourceCommandItem(items, root, dialect, track, command);
  const auto* item = itemById(itemTree, commandItem);
  expect(item != nullptr && item->kind == ItemKind::Command, "source command helper should add a command item");
  expect(item->detailKind == "probe.program" && item->name == "Program",
         "source command helper should reuse dialect display metadata");
  expect(item->description == "program 5", "source command helper should format command fields consistently");
  expect(sameRange(item->range, command.range), "source command helper should preserve the command source range");
  expect(itemById(itemTree, root)->children == std::vector<ItemId>{commandItem},
         "source command helper should attach command items under the requested parent");

  bool rejectedTrailingBytes = false;
  try {
    const std::array<u8, 3> trailingProgramBytes{0x80, 0x05, 0xaa};
    static_cast<void>(addProbeCommand<ProbeProgramCommand>(
        builder, dialect, Address{2}, probeRange(2, trailingProgramBytes.size()), trailingProgramBytes));
  } catch (const std::invalid_argument&) {
    rejectedTrailingBytes = true;
  }
  expect(rejectedTrailingBytes, "track builder should reject command bytes not consumed by the local parser");
  expect(track.commands.size() == 1, "rejected command bytes should not mutate the track program");
}

void sequenceDialectPreservesCommandPlaybackStatus() {
  const SequenceDialect dialect = probeSequenceDialect();

  const auto* note = dialect.kindForName(ProbeNoteCommand::kind);
  expect(note != nullptr && note->playbackStatus == CommandPlaybackStatus::AffectsPlayback,
         "commands without an explicit status should default to playback-affecting");

  const auto* jump = dialect.kindForName(ProbeJumpCommand::kind);
  expect(jump != nullptr && jump->playbackStatus == CommandPlaybackStatus::AffectsControlFlow,
         "control-flow commands should preserve their explicit playback status");

  const auto* end = dialect.kindForName(ProbeEndCommand::kind);
  expect(end != nullptr && end->playbackStatus == CommandPlaybackStatus::StopsPlayback,
         "terminal commands should preserve their explicit playback status");
}

void vmCommandCursorRecordsCommandAnnotations() {
  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> diagnostics;
  const std::array<u8, 2> bytes{0xc4, 0x40};
  VmCommandCursor cmd(CommandPhase::Decode, probeRange(0, bytes.size()), bytes, &sourceMap, &diagnostics);

  const u8 pan = cmd.name("Pan").semantic(SequenceSemantic::Pan).u8("pan");
  const CommandFlow flow = cmd.next();
  expect(flow.kind == FlowKind::Next && !flow.truncated, "cursor next flow should stay simple for valid commands");
  expect(pan == 0x40, "cursor should read valid command operands");

  const SourceMap map = sourceMap.finish();
  const auto& annotation = map.get(cmd.annotation());
  expect(annotation.label == "Pan" && annotation.localKind == "pan",
         "cursor should record command display name and slugified kind");
  expect(annotation.role == SourceRole::Command && annotation.sequenceSemantic == SequenceSemantic::Pan,
         "cursor should record command role and broad sequence semantic");
  expect(annotation.fields.size() == 2, "cursor should record opcode and operand fields");
  expect(annotation.fields[0].name == "opcode" && std::get<u64>(annotation.fields[0].value) == 0xc4,
         "cursor should record opcode field");
  expect(annotation.fields[1].name == "pan" && std::get<u64>(annotation.fields[1].value) == 0x40 &&
             sameRange(annotation.fields[1].range, probeRange(1, 1)),
         "cursor should record operand value and source range");
  expect(diagnostics.empty(), "valid cursor reads should not produce diagnostics");
}

void vmCommandCursorSupportsKindOverrideAndTargetLinks() {
  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  const std::array<u8, 3> bytes{0x94, 0x12, 0x34};
  VmCommandCursor cmd(CommandPhase::Decode, probeRange(0, bytes.size()), bytes, &sourceMap);

  const Address destination =
      cmd.name("End of Track").kind("jump").semantic(SequenceSemantic::Jump).address16be("destination");
  const CommandFlow flow = cmd.jump(destination);
  expect(flow.kind == FlowKind::Jump && flow.destination && flow.destination->value == 0x1234,
         "cursor jump flow should preserve destination");

  const SourceMap map = sourceMap.finish();
  const auto& annotation = map.get(cmd.annotation());
  expect(annotation.localKind == "jump", "cursor kind override should replace slugified label");
  expect(annotation.links.size() == 1 && annotation.links[0].role == SourceLinkRole::JumpTarget,
         "cursor jump helper should record a structured target link");
  const auto* target = std::get_if<SourceRange>(&annotation.links[0].target);
  expect(target != nullptr && target->source == SourceId{0} && target->offset == 0x1234 && target->size == 1,
         "cursor target link should point at the destination source range");
}

void vmCommandCursorStickyFailsMalformedReads() {
  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> diagnostics;
  const std::array<u8, 1> bytes{0xc4};
  VmCommandCursor cmd(CommandPhase::Decode, probeRange(0, bytes.size()), bytes, &sourceMap, &diagnostics);

  bool threw = false;
  try {
    static_cast<void>(cmd.name("Pan").semantic(SequenceSemantic::Pan).u8("pan"));
  } catch (const CommandReadTruncated&) {
    threw = true;
  }
  const CommandFlow flow = cmd.next();
  expect(threw, "cursor should stop command parsing when a required operand is missing");
  expect(flow.kind == FlowKind::Stop && flow.truncated,
         "cursor should turn normal flow helpers into truncated stop flow after a failed read");
  expect(diagnostics.size() == 1 && diagnostics[0].message == "Truncated sequence command while reading pan",
         "cursor should report malformed reads without per-command boilerplate");

  const SourceMap map = sourceMap.finish();
  const auto& annotation = map.get(cmd.annotation());
  const auto truncated =
      std::ranges::find_if(annotation.fields, [](const SourceField& field) { return field.name == "truncated"; });
  expect(truncated != annotation.fields.end() && std::get<bool>(truncated->value),
         "cursor should mark malformed command annotations as truncated");
  expect(annotation.fields.size() == 2, "malformed cursor should record opcode and truncated marker only");
}

void trackProgramBuilderRejectsDuplicateCommandAddresses() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);

  bool rejectedDuplicateAddress = false;
  try {
    static_cast<void>(addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0},
                                                           probeRange(2, programBytes.size()), programBytes));
  } catch (const std::invalid_argument&) {
    rejectedDuplicateAddress = true;
  }
  expect(rejectedDuplicateAddress, "track builder should reject duplicate source command addresses");
  expect(track.commands.size() == 1, "duplicate-address rejection should not mutate the track program");
}

void collectionIssueHelpersValidateStoredStatus() {
  const CollectionIssue missingSequence = missingSequenceIssue();
  expect(missingSequence.severity == Severity::Warning && missingSequence.code == "missing-sequence",
         "missing sequence helper should create a warning issue");
  const std::vector<CollectionIssue> missingIssues{missingSequence};
  expect(validatedCollectionStatus(CollectionStatus::Complete, missingIssues) == CollectionStatus::Incomplete,
         "missing issues should prevent complete collection status");

  const CollectionIssue missingInstrument = missingInstrumentSetIssue(AssetId{7});
  expect(missingInstrument.severity == Severity::Error && missingInstrument.asset == AssetId{7},
         "missing instrument helper should preserve a broken asset reference");

  const CollectionIssue ambiguous = ambiguousMatchIssue("multiple banks match");
  const std::vector<CollectionIssue> ambiguousIssues{ambiguous};
  expect(validatedCollectionStatus(CollectionStatus::Complete, ambiguousIssues) == CollectionStatus::Ambiguous,
         "ambiguous match issue should validate complete status to ambiguous");

  const CollectionIssue removed = removedStaleAssetIssue();
  const std::vector<CollectionIssue> removedIssues{removed};
  expect(validatedCollectionStatus(CollectionStatus::Complete, removedIssues) == CollectionStatus::Stale,
         "removed asset issue should validate complete status to stale");

  DesiredCollection explicitStale{
      .status = CollectionStatus::Stale,
      .issues = {missingSampleCollectionIssue()},
  };
  expect(validatedCollectionStatus(explicitStale) == CollectionStatus::Stale,
         "explicit non-complete status should remain stored instead of being derived from issues");
}

}  // namespace

void runValueSequenceModelTests() {
  levelScaleRoundTripsMidiValues();
  byteReaderChecksBoundsAndEndian();
  commandReaderRejectsUnterminatedVariableLengthOperands();
  sourceCommandsPreserveBytesOperandsAndDialectDisplay();
  sequenceDialectPreservesCommandPlaybackStatus();
  vmCommandCursorRecordsCommandAnnotations();
  vmCommandCursorSupportsKindOverrideAndTargetLinks();
  vmCommandCursorStickyFailsMalformedReads();
  trackProgramBuilderRejectsDuplicateCommandAddresses();
  collectionIssueHelpersValidateStoredStatus();
}

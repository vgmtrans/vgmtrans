/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

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

  const auto* note = dialect.handlerForKind(ProbeNoteCommand::kind);
  expect(note != nullptr && note->playbackStatus == CommandPlaybackStatus::AffectsPlayback,
         "commands without an explicit status should default to playback-affecting");

  const auto* jump = dialect.handlerForKind(ProbeJumpCommand::kind);
  expect(jump != nullptr && jump->playbackStatus == CommandPlaybackStatus::AffectsControlFlow,
         "control-flow commands should preserve their explicit playback status");

  const auto* end = dialect.handlerForKind(ProbeEndCommand::kind);
  expect(end != nullptr && end->playbackStatus == CommandPlaybackStatus::StopsPlayback,
         "terminal commands should preserve their explicit playback status");
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
  trackProgramBuilderRejectsDuplicateCommandAddresses();
  collectionIssueHelpersValidateStoredStatus();
}

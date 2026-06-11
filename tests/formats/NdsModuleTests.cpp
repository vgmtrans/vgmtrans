/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/PerformanceMidiRenderer.h"
#include "value/core/ScanTypes.h"
#include "value/core/SequenceVm.h"
#include "value/formats/NDS/NdsSequenceDialect.h"
#include "value/formats/NDS/NdsSequenceProgram.h"

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::nds;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void ndsSequenceDialectDecodesAndRendersNoteWaitCommands() {
  std::vector<u8> bytes(0x140);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  bytes[trackStart + 0] = 0xc7;
  bytes[trackStart + 1] = 0x01;
  bytes[trackStart + 2] = 0x3c;
  bytes[trackStart + 3] = 0x64;
  bytes[trackStart + 4] = 0x18;
  bytes[trackStart + 5] = 0xe1;
  bytes[trackStart + 6] = 0x78;
  bytes[trackStart + 7] = 0x00;
  bytes[trackStart + 8] = 0x80;
  bytes[trackStart + 9] = 0x06;
  bytes[trackStart + 10] = 0xff;

  const SequenceDialect dialect = ndsSequenceDialect();
  const auto starts = ndsSequenceTrackStarts(ByteReader(SourceId{4}, bytes), sequenceOffset, trackStart + 11);
  expect(starts.size() == 1 && starts[0] == trackStart, "NDS SSEQ track-start discovery should find the primary track");

  const TrackProgram track =
      decodeNdsSequenceTrack(ByteReader(SourceId{4}, bytes), dialect, sequenceOffset, trackStart + 11, trackStart, 0);
  expect(track.commands.size() == 5, "NDS SSEQ dialect should decode all fixture commands");
  expect(dialect.describe(track, track.commands[0]).detailKind == "nds.note-wait",
         "NDS SSEQ dialect should decode note-wait as a local command");
  expect(dialect.describe(track, track.commands[1]).detailKind == "nds.note",
         "NDS SSEQ dialect should decode source note opcodes as local commands");
  expect(track.operandsFor(track.commands[1]).size() == 2 &&
             std::get<u64>(track.operandsFor(track.commands[1])[0].value) == 0x64 &&
             std::get<u64>(track.operandsFor(track.commands[1])[1].value) == 0x18,
         "NDS SSEQ note command should preserve velocity and duration operands");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "NDS SSEQ fixture should render without diagnostics");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 30,
         "NDS note-wait should make notes advance time before the rest command");

  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  expect(midi.tracks.size() == 1, "NDS SSEQ MIDI rendering should preserve one track");
  const auto& events = midi.tracks[0].events;
  expect(std::get<NoteDuration>(events[0]).tick == 0 && std::get<NoteDuration>(events[0]).duration == 24,
         "NDS SSEQ note should render at the current tick with its source duration");
  expect(std::get<Tempo>(events[1]).tick == 24 && std::get<Tempo>(events[1]).microsecondsPerQuarter == 500000,
         "NDS SSEQ tempo should convert BPM to microseconds per quarter");
  expect(std::get<EndOfTrack>(events.back()).tick == 30, "NDS SSEQ MIDI rendering should preserve VM end tick");

  bytes[trackStart + 0] = 0x81;
  bytes[trackStart + 1] = 0x81;
  bytes[trackStart + 2] = 0x05;
  bytes[trackStart + 3] = 0xff;
  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.name = "program.sseq"},
      .reader = ByteReader(SourceId{4}, bytes),
      .ids = ids,
  };
  const auto asset = parseNdsSequenceProgram(input,
                                             AssetId{7},
                                             NdsSequenceRange{
                                                 .offset = sequenceOffset,
                                                 .size = 0x40,
                                                 .sequenceEnd = trackStart + 4,
                                             },
                                             "Program",
                                             AssetId{3});
  expect(asset.program.referencedInstruments.size() == 1,
         "NDS sequence program should reference instruments used by source program commands");
  const auto& ref = asset.program.referencedInstruments[0];
  expect(ref.asset == AssetId{3} && ref.bank == 1 && ref.program == 5,
         "NDS program references should decode bank and program from the source varlen value");
  expect(ref.range && ref.range->offset == trackStart && ref.range->size == 3,
         "NDS program references should preserve the source program command range");
}

void ndsSequenceDialectExecutesCallAndReturn() {
  std::vector<u8> bytes(0x160);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 subroutineOffset = trackStart + 0x10;
  constexpr u32 subroutineRelative = subroutineOffset - trackStart;

  bytes[trackStart + 0] = 0xc7;
  bytes[trackStart + 1] = 0x01;
  bytes[trackStart + 2] = 0x95;
  bytes[trackStart + 3] = static_cast<u8>(subroutineRelative & 0xff);
  bytes[trackStart + 4] = static_cast<u8>((subroutineRelative >> 8) & 0xff);
  bytes[trackStart + 5] = static_cast<u8>((subroutineRelative >> 16) & 0xff);
  bytes[trackStart + 6] = 0x80;
  bytes[trackStart + 7] = 0x07;
  bytes[trackStart + 8] = 0xff;

  bytes[subroutineOffset + 0] = 0x3c;
  bytes[subroutineOffset + 1] = 0x64;
  bytes[subroutineOffset + 2] = 0x05;
  bytes[subroutineOffset + 3] = 0xfd;

  const SequenceDialect dialect = ndsSequenceDialect();
  const TrackProgram track = decodeNdsSequenceTrack(
      ByteReader(SourceId{5}, bytes), dialect, sequenceOffset, subroutineOffset + 4, trackStart, 0);
  expect(track.commands.size() == 6, "NDS call fixture should decode call target and fallthrough blocks");

  const auto call = std::ranges::find_if(track.commands, [&](const SourceCommand& command) {
    return dialect.describe(track, command).detailKind == "nds.call";
  });
  expect(call != track.commands.end(), "NDS call fixture should preserve the call command");
  expect(call->range.offset == trackStart + 2 && call->range.size == 4,
         "NDS call command should preserve its source range");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "NDS call fixture should render without diagnostics");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 12,
         "NDS call fixture should return to the fallthrough rest command");

  const auto note = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* typed = std::get_if<NotePerformanceEvent>(&event);
    return typed != nullptr;
  });
  expect(note != performance.tracks[0].events.end(), "NDS call fixture should emit the subroutine note");
  const auto& noteEvent = std::get<NotePerformanceEvent>(*note);
  expect(noteEvent.header.tick == 0 && noteEvent.key == 60.0 && noteEvent.durationTicks == 5,
         "NDS subroutine note should render at the call tick and use source duration");
}

void ndsSequenceDialectDiscoversSecondaryTrackStarts() {
  std::vector<u8> bytes(0x180);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 primaryStart = trackStart + 8;
  constexpr u32 secondaryStart = trackStart + 0x20;
  constexpr u32 secondaryRelative = secondaryStart - trackStart;

  bytes[trackStart + 0] = 0xfe;
  bytes[trackStart + 1] = 0x00;
  bytes[trackStart + 2] = 0x00;
  bytes[trackStart + 3] = 0x93;
  bytes[trackStart + 4] = 0x02;
  bytes[trackStart + 5] = static_cast<u8>(secondaryRelative & 0xff);
  bytes[trackStart + 6] = static_cast<u8>((secondaryRelative >> 8) & 0xff);
  bytes[trackStart + 7] = static_cast<u8>((secondaryRelative >> 16) & 0xff);
  bytes[primaryStart] = 0xff;

  bytes[secondaryStart + 0] = 0x80;
  bytes[secondaryStart + 1] = 0x03;
  bytes[secondaryStart + 2] = 0xff;

  const auto starts = ndsSequenceTrackStarts(ByteReader(SourceId{6}, bytes), sequenceOffset, secondaryStart + 3);
  expect(starts.size() == 2 && starts[0] == primaryStart && starts[1] == secondaryStart,
         "NDS SSEQ track-start discovery should include bootstrap secondary tracks");

  const SequenceDialect dialect = ndsSequenceDialect();
  const TrackProgram secondary =
      decodeNdsSequenceTrack(ByteReader(SourceId{6}, bytes), dialect, sequenceOffset, secondaryStart + 3,
                             starts[1], 1);
  expect(secondary.sourceTrackNumber == 1 && secondary.commands.size() == 2,
         "NDS secondary track should decode independently from the primary bootstrap");
  expect(dialect.describe(secondary, secondary.commands[0]).detailKind == "nds.rest",
         "NDS secondary track should preserve decoded source commands");
}

void ndsSequenceDialectPreservesIgnoredNoOpOperands() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xe0;
  bytes[trackStart + 1] = 0x12;
  bytes[trackStart + 2] = 0x34;
  bytes[trackStart + 3] = 0xff;

  const SequenceDialect dialect = ndsSequenceDialect();
  const TrackProgram track =
      decodeNdsSequenceTrack(ByteReader(SourceId{7}, bytes), dialect, sequenceOffset, trackStart + 4, trackStart, 0);
  expect(track.commands.size() == 2, "NDS no-op fixture should decode the ignored command and end command");

  const SourceCommand& noOp = track.commands[0];
  expect(dialect.describe(track, noOp).detailKind == "nds.no-op",
         "NDS ignored opcode should stay typed as a local no-op command");
  expect(track.bytesFor(noOp).size() == 3 && track.bytesFor(noOp)[0] == 0xe0,
         "NDS no-op should preserve the original command bytes");

  const auto operands = track.operandsFor(noOp);
  expect(operands.size() == 1 && operands[0].name == "bytes" &&
             std::get<std::string>(operands[0].value) == "12 34",
         "NDS no-op should preserve ignored operand bytes as decoded command data");
  expect(operands[0].range.offset == trackStart + 1 && operands[0].range.size == 2,
         "NDS no-op ignored operand bytes should preserve their source range");
}

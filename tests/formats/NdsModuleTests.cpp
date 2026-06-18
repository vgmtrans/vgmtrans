/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/ScanTypes.h"
#include "value/sequence/SequenceVm.h"
#include "value/formats/NDS/NdsSequence.h"
#include "value/formats/NDS/NdsSynth.h"
#include "value/validation/SynthValidation.h"

#include <array>
#include <optional>
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

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
}

void writeLe32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
  bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
  bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
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

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const auto starts = ndsSequenceTrackStarts(ByteReader(SourceId{4}, bytes), sequenceOffset, trackStart + 11);
  expect(starts.size() == 1 && starts[0] == trackStart, "NDS SSEQ track-start discovery should find the primary track");

  ScanIdAllocator annotationIds;
  SourceMapBuilder sourceMap([&annotationIds]() { return annotationIds.nextSourceAnnotationId(); });
  std::vector<Diagnostic> decodeDiagnostics;
  const TrackProgram track = decodeNdsSequenceTrack(ByteReader(SourceId{4}, bytes), descriptor, sequenceOffset,
                                                    trackStart + 11, trackStart, 0, false, &sourceMap,
                                                    &decodeDiagnostics);
  expect(track.commands.size() == 5, "NDS SSEQ dialect should decode all fixture commands");
  expect(dialect.describe(track, track.commands[0]).detailKind == "nds.note-wait",
         "NDS SSEQ dialect should decode note-wait as a local command");
  expect(dialect.describe(track, track.commands[1]).detailKind == "nds.note",
         "NDS SSEQ dialect should decode source note opcodes as local commands");
  expect(track.operandsFor(track.commands[1]).size() == 3 &&
             std::get<u64>(track.operandsFor(track.commands[1])[0].value) == 0x3c &&
             std::get<u64>(track.operandsFor(track.commands[1])[1].value) == 0x64 &&
             std::get<u64>(track.operandsFor(track.commands[1])[2].value) == 0x18,
         "NDS SSEQ note command should preserve key, velocity, and duration operands");
  const SourceMap annotations = sourceMap.finish();
  const auto noteAnnotations = annotations.withSequenceSemantic(SourceId{4}, SequenceSemantic::Note);
  expect(noteAnnotations.size() == 1, "NDS SSEQ note command should publish a source annotation");
  const auto& noteAnnotation = annotations.get(noteAnnotations[0]);
  expect(noteAnnotation.range.offset == trackStart + 2 && noteAnnotation.range.size == 3,
         "NDS SSEQ note annotation should use the exact decoded command range");
  const auto hasNoteField = [&](std::string_view name) {
    return std::ranges::any_of(noteAnnotation.fields, [&](const SourceField& field) { return field.name == name; });
  };
  expect(hasNoteField("opcode") && hasNoteField("key") && hasNoteField("velocity") && hasNoteField("duration"),
         "NDS SSEQ note annotation should record opcode and operand fields");
  expect(decodeDiagnostics.empty(), "NDS SSEQ cursor decode should not emit diagnostics for valid commands");

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
  expect(std::get<MidiPort>(events[0]).port == 0, "NDS SSEQ MIDI rendering should emit MIDI port metadata");
  expect(std::get<NoteDuration>(events[1]).tick == 0 && std::get<NoteDuration>(events[1]).duration == 24,
         "NDS SSEQ note should render at the current tick with its source duration");
  expect(std::get<Tempo>(events[2]).tick == 24 && std::get<Tempo>(events[2]).microsecondsPerQuarter == 500000,
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
  SourceMapBuilder programSourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> programDiagnostics;
  const auto asset = parseNdsSequenceProgram(input, AssetId{7},
                                             NdsSequenceRange{
                                                 .offset = sequenceOffset,
                                                 .size = 0x40,
                                                 .sequenceEnd = trackStart + 4,
                                             },
                                             "Program", ScanInstrumentSetRef{AssetId{3}}, &programSourceMap,
                                             &programDiagnostics);
  expect(asset.program.referencedInstruments.size() == 1,
         "NDS sequence program should reference instruments used by source program commands");
  const auto& ref = asset.program.referencedInstruments[0];
  expect(ref.asset == AssetId{3} && ref.bank == 1 && ref.program == 5,
         "NDS program references should decode bank and program from the source varlen value");
  expect(ref.range && ref.range->offset == trackStart && ref.range->size == 3,
         "NDS program references should preserve the source program command range");
  const SourceMap programAnnotations = programSourceMap.finish();
  const auto programAnnotationIds = programAnnotations.withSequenceSemantic(SourceId{4}, SequenceSemantic::Program);
  expect(programAnnotationIds.size() == 1, "NDS program command should publish one program annotation");
  const auto& programAnnotation = programAnnotations.get(programAnnotationIds[0]);
  const auto instrumentLink = std::ranges::find_if(programAnnotation.links, [](const SourceLink& link) {
    return link.role == SourceLinkRole::UsesInstrument;
  });
  expect(instrumentLink != programAnnotation.links.end(),
         "NDS program command should record a structured instrument source link");
  const auto* instrumentTarget = std::get_if<ObjectRef>(&instrumentLink->target);
  expect(instrumentTarget != nullptr && instrumentTarget->kind == ObjectKind::Instrument &&
             !instrumentTarget->asset.valid() && instrumentTarget->index0 == 1 && instrumentTarget->index1 == 5,
         "NDS program source link should preserve unresolved bank/program selectors");
  expect(programDiagnostics.empty(), "NDS program source-link decode should not emit diagnostics");

  bytes[trackStart + 0] = 0xd5;
  bytes[trackStart + 1] = 0x7f;
  bytes[trackStart + 2] = 0xff;
  const TrackProgram expressionTrack =
      decodeNdsSequenceTrack(ByteReader(SourceId{4}, bytes), descriptor, sequenceOffset, trackStart + 3, trackStart, 0);
  expect(expressionTrack.commands.size() == 2 &&
             dialect.describe(expressionTrack, expressionTrack.commands[0]).detailKind == "nds.expression",
         "NDS expression opcode should decode as a musical command");
  const SequenceProgram expressionProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {expressionTrack},
  };
  const MidiSequence expressionMidi =
      PerformanceMidiRenderer().render(SequenceVm(LoopPolicy::PlayOnce).render(expressionProgram, dialect));
  expect(std::holds_alternative<Expression>(expressionMidi.tracks[0].events[1]),
         "NDS expression opcode should render as MIDI expression");
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

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const TrackProgram track = decodeNdsSequenceTrack(ByteReader(SourceId{5}, bytes), descriptor, sequenceOffset,
                                                    subroutineOffset + 4, trackStart, 0);
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

  const TrackProgram linearizedTrack = decodeNdsSequenceTrack(
      ByteReader(SourceId{5}, bytes), descriptor, sequenceOffset, subroutineOffset + 4, trackStart, 0, true);
  expect(linearizedTrack.commands.size() == 6,
         "NDS linearized call fixture should still decode call target and fallthrough blocks");
  const SequenceProgram linearizedProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {linearizedTrack},
  };
  const PerformanceSequence linearizedPerformance = SequenceVm(LoopPolicy::PlayOnce).render(linearizedProgram, dialect);
  expect(linearizedPerformance.diagnostics.empty(),
         "NDS linearized call fixture should render without missing-target diagnostics");

  std::vector<u8> overlapBytes(0x140);
  overlapBytes[trackStart + 0] = 0x95;
  overlapBytes[trackStart + 1] = 0x05;
  overlapBytes[trackStart + 2] = 0x00;
  overlapBytes[trackStart + 3] = 0x00;
  overlapBytes[trackStart + 4] = 0xc1;
  overlapBytes[trackStart + 5] = 0x3c;
  overlapBytes[trackStart + 6] = 0x64;
  overlapBytes[trackStart + 7] = 0x01;
  overlapBytes[trackStart + 8] = 0xfd;
  const TrackProgram overlapTrack = decodeNdsSequenceTrack(ByteReader(SourceId{8}, overlapBytes), descriptor,
                                                           sequenceOffset, trackStart + 9, trackStart, 0, true);
  expect(overlapTrack.commands.size() == 4,
         "NDS linearized overlap fixture should split fallthrough from call-target bytes");
  expect(dialect.describe(overlapTrack, overlapTrack.commands[1]).detailKind == "nds.end",
         "NDS linearized overlap fixture should stop before overlapping a queued call target");
  const SequenceProgram overlapProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {overlapTrack},
  };
  const PerformanceSequence overlapPerformance = SequenceVm(LoopPolicy::PlayOnce).render(overlapProgram, dialect);
  expect(overlapPerformance.diagnostics.empty(),
         "NDS linearized overlap fixture should render without unpaired-return diagnostics");
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

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const TrackProgram secondary = decodeNdsSequenceTrack(ByteReader(SourceId{6}, bytes), descriptor, sequenceOffset,
                                                        secondaryStart + 3, starts[1], 1);
  expect(secondary.sourceTrackNumber == 1 && secondary.commands.size() == 2,
         "NDS secondary track should decode independently from the primary bootstrap");
  expect(dialect.describe(secondary, secondary.commands[0]).detailKind == "nds.rest",
         "NDS secondary track should preserve decoded source commands");
}

void ndsSequenceTrackStartDiscoveryKeepsMalformedBootstrapCommands() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xfe;
  bytes[trackStart + 1] = 0x00;
  bytes[trackStart + 2] = 0x00;
  bytes[trackStart + 3] = 0x80;
  bytes[trackStart + 4] = 0x81;

  const auto starts = ndsSequenceTrackStarts(ByteReader(SourceId{12}, bytes), sequenceOffset, trackStart + 5);
  expect(starts.size() == 1 && starts.front() == trackStart + 3,
         "NDS track-start discovery should not skip an unterminated bootstrap variable-length command");

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const TrackProgram track =
      decodeNdsSequenceTrack(ByteReader(SourceId{12}, bytes), descriptor, sequenceOffset, trackStart + 5,
                             starts.front(), 0);
  expect(track.commands.size() == 1 && dialect.describe(track, track.commands[0]).detailKind == "nds.truncated",
         "NDS malformed bootstrap command should be preserved as a truncated source command");
}

void ndsSequenceDialectPreservesIgnoredCommandOperands() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xe0;
  bytes[trackStart + 1] = 0x12;
  bytes[trackStart + 2] = 0x34;
  bytes[trackStart + 3] = 0xff;

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const TrackProgram track =
      decodeNdsSequenceTrack(ByteReader(SourceId{7}, bytes), descriptor, sequenceOffset, trackStart + 4, trackStart, 0);
  expect(track.commands.size() == 2, "NDS ignored-command fixture should decode the ignored command and end command");

  const SourceCommand& ignored = track.commands[0];
  expect(dialect.describe(track, ignored).detailKind == "nds.modulation-delay",
         "NDS ignored opcode should stay typed as its source-driver command");
  expect(track.bytesFor(ignored).size() == 3 && track.bytesFor(ignored)[0] == 0xe0,
         "NDS ignored command should preserve the original command bytes");

  const auto operands = track.operandsFor(ignored);
  expect(operands.size() == 1 && operands[0].name == "bytes" && std::get<std::string>(operands[0].value) == "12 34",
         "NDS ignored command should preserve ignored operand bytes as decoded command data");
  expect(operands[0].range.offset == trackStart + 1 && operands[0].range.size == 2,
         "NDS ignored command operand bytes should preserve their source range");
}

void ndsSequenceDialectKeepsEmptyPlaceholderTrack() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  const auto starts = ndsSequenceTrackStarts(ByteReader(SourceId{8}, bytes), sequenceOffset, trackStart);
  expect(starts.size() == 1 && starts.front() == trackStart,
         "NDS empty placeholder sequences should keep their first empty track");

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const TrackProgram track =
      decodeNdsSequenceTrack(ByteReader(SourceId{8}, bytes), descriptor, sequenceOffset, trackStart, trackStart, 0);
  expect(track.commands.empty(), "NDS empty placeholder tracks should not decode padding as commands");
}

void ndsSequenceDialectMarksUnterminatedVarLenAsTruncated() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0x80;
  bytes[trackStart + 1] = 0x81;

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const TrackProgram track = decodeNdsSequenceTrack(ByteReader(SourceId{10}, bytes), descriptor, sequenceOffset,
                                                    trackStart + 2, trackStart, 0);
  expect(track.commands.size() == 1, "NDS unterminated variable-length command should decode as one command");
  expect(dialect.describe(track, track.commands[0]).detailKind == "nds.truncated",
         "NDS unterminated variable-length command should use the truncated-command fallback");
  expect(track.bytesFor(track.commands[0]).size() == 1 && track.bytesFor(track.commands[0])[0] == 0x80,
         "NDS truncated command should preserve only the opcode byte");
}

void ndsMalformedRecoveryKeepsExecutableJumps() {
  std::vector<u8> bytes(0x180);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 subroutineOffset = trackStart + 0x20;
  constexpr u32 subroutineRelative = subroutineOffset - trackStart;

  bytes[trackStart + 0] = 0xc7;
  bytes[trackStart + 1] = 0x01;
  bytes[trackStart + 2] = 0x95;
  bytes[trackStart + 3] = static_cast<u8>(subroutineRelative & 0xff);
  bytes[trackStart + 4] = static_cast<u8>((subroutineRelative >> 8) & 0xff);
  bytes[trackStart + 5] = static_cast<u8>((subroutineRelative >> 16) & 0xff);
  bytes[trackStart + 6] = 0x80;
  bytes[trackStart + 7] = 0x01;
  bytes[trackStart + 8] = 0xff;

  bytes[subroutineOffset + 0] = 0x3c;
  bytes[subroutineOffset + 1] = 0x64;
  bytes[subroutineOffset + 2] = 0x02;
  bytes[subroutineOffset + 3] = 0x94;
  bytes[subroutineOffset + 4] = static_cast<u8>(subroutineRelative & 0xff);
  bytes[subroutineOffset + 5] = static_cast<u8>((subroutineRelative >> 8) & 0xff);
  bytes[subroutineOffset + 6] = static_cast<u8>((subroutineRelative >> 16) & 0xff);

  const auto& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const TrackProgram track = decodeNdsSequenceTrack(ByteReader(SourceId{9}, bytes), descriptor, sequenceOffset,
                                                    subroutineOffset + 7, trackStart, 0, true);
  const auto jump = std::ranges::find_if(track.commands, [&](const SourceCommand& command) {
    return dialect.describe(track, command).detailKind == "nds.jump";
  });
  expect(jump != track.commands.end(), "NDS malformed recovery should preserve recovered jumps as jump commands");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .behavior = SequenceProgramBehavior{.commandLimit = 64},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "NDS recovered jump loop should stop without hitting the command limit");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 2,
         "NDS recovered jump loop should render one pass through the subroutine loop");
}

void ndsSynthParserKeepsInfiniteReleaseOutOfPreciseSeconds() {
  std::vector<u8> bytes(0x80);
  writeLe32(bytes, 0x38, 1);
  writeLe32(bytes, 0x3c, (0x40u << 8) | 0x01);
  writeLe16(bytes, 0x40, 0);
  writeLe16(bytes, 0x42, 0);
  bytes[0x44] = 60;
  bytes[0x45] = 0x6d;
  bytes[0x46] = 0x20;
  bytes[0x47] = 0x7f;
  bytes[0x48] = 0x7f;
  bytes[0x49] = 64;

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{11}, .name = "bank.sbnk", .size = bytes.size()},
      .reader = ByteReader(SourceId{11}, bytes),
      .ids = ids,
  };
  ScanResultBuilder out(input, "NDS");
  const auto psg = out.reserveSampleCollection();
  std::array<std::optional<ScanSampleCollectionRef>, 4> waves{};
  waves[0] = out.reserveSampleCollection();

  const auto bank =
      parseNdsInstrumentSet(input, AssetId{2}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())},
                            "Bank", out, psg, waves);
  expect(bank.instruments.size() == 1 && bank.instruments[0].regions.size() == 1,
         "NDS synth parser should keep a valid instrument with infinite release");
  const Envelope& envelope = bank.instruments[0].regions[0].envelope;
  expect(envelope.release == kEnvelopeInfinite, "NDS infinite release should remain explicit in coarse envelope units");
  expect(!envelope.releaseSeconds.has_value(),
         "NDS infinite release should not use a negative precise-seconds sentinel");
  expect(validateInstrumentSet(bank).empty(), "NDS infinite release should pass synth validation");

  bytes[0x45] = 0x80;
  const auto malformedBank =
      parseNdsInstrumentSet(input, AssetId{4}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())},
                            "Malformed Bank", out, psg, waves);
  expect(malformedBank.instruments.empty(),
         "NDS synth parser should skip regions with malformed envelope-rate bytes");
}

void ndsSynthParserDerivesAdpcmLengthsSafely() {
  std::vector<u8> bytes(0x60);
  bytes[0] = 'S';
  bytes[1] = 'W';
  bytes[2] = 'A';
  bytes[3] = 'R';
  bytes[4] = 0xff;
  bytes[5] = 0xfe;
  bytes[6] = 0x00;
  bytes[7] = 0x01;
  writeLe32(bytes, 0x38, 1);
  writeLe32(bytes, 0x3c, 0x40);
  bytes[0x40] = 2;
  bytes[0x41] = 0;
  writeLe16(bytes, 0x42, 32768);
  writeLe16(bytes, 0x44, 0);
  writeLe16(bytes, 0x46, 0);
  writeLe16(bytes, 0x48, 2);
  bytes[0x50] = 0;
  bytes[0x51] = 0;
  bytes[0x52] = 0;
  bytes[0x53] = 0;

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{12}, .name = "wave.swar", .size = bytes.size()},
      .reader = ByteReader(SourceId{12}, bytes),
      .ids = ids,
  };

  const auto wave = parseNdsWaveArchive(input, AssetId{5},
                                        NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())}, "Wave");
  expect(wave.samples.samples.size() == 1, "NDS parser should keep non-looping ADPCM with loop offset zero");
  const Sample& sample = wave.samples.samples[0];
  expect(sample.encodedData.offset == 0x50 && sample.encodedData.size == 4,
         "NDS ADPCM encoded data should skip the predictor header");
  expect(!sample.loop.enabled && sample.loop.start == 0 && sample.loop.length == 9,
         "NDS non-looping ADPCM should keep sane decoded loop metadata");

  bytes[0x41] = 1;
  const auto malformedLoop =
      parseNdsWaveArchive(input, AssetId{6}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())},
                          "Malformed Wave");
  expect(malformedLoop.samples.samples.empty(),
         "NDS parser should skip looped ADPCM with an unusable loop offset instead of underflowing");
}

void ndsWaveArchiveReportsTruncatedSampleHeaders() {
  std::vector<u8> bytes(0x44);
  bytes[0] = 'S';
  bytes[1] = 'W';
  bytes[2] = 'A';
  bytes[3] = 'R';
  bytes[4] = 0xff;
  bytes[5] = 0xfe;
  bytes[6] = 0x00;
  bytes[7] = 0x01;
  writeLe32(bytes, 0x38, 1);
  writeLe32(bytes, 0x3c, 0x40);

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{13}, .name = "truncated-wave.swar", .size = bytes.size()},
      .reader = ByteReader(SourceId{13}, bytes),
      .ids = ids,
  };
  ScanResultBuilder out(input, "NDS");

  const auto wave =
      parseNdsWaveArchive(input, AssetId{7}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())},
                          "Truncated Wave", &out);
  expect(wave.samples.samples.empty(), "NDS parser should skip truncated SWAR sample headers");

  const ScanResult result = out.finish();
  expect(!result.diagnostics.empty(), "NDS parser should diagnose truncated SWAR sample headers");
  expect(result.diagnostics[0].message.find("SWAR sample header") != std::string::npos,
         "NDS SWAR diagnostic should name the truncated field");
}

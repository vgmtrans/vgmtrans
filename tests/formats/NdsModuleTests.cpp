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

#include "ValueFormatTestSupport.h"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

const SourceAnnotation* annotationWithKind(const SourceMap& sourceMap, SourceId source, SourceRole role,
                                           std::string_view localKind) {
  const auto annotations = sourceMap.withRole(source, role);
  for (const SourceAnnotationId id : annotations) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    if (annotation.localKind == localKind) {
      return &annotation;
    }
  }
  return nullptr;
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

TrackProgram decodeTestTrack(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                             SourceMapBuilder* sourceMap = nullptr, std::vector<Diagnostic>* diagnostics = nullptr) {
  const SequenceProgram program =
      decodeNdsSequence(reader, AssetId{7}, NdsSequenceRange{.offset = sequenceOffset, .sequenceEnd = sequenceEnd},
                        sourceMap, diagnostics);
  const auto track = std::ranges::find_if(program.tracks, [=](const TrackProgram& candidate) {
    return candidate.startAddress.value == startOffset && candidate.sourceTrackNumber == trackIndex;
  });
  if (track == program.tracks.end()) {
    throw std::runtime_error("NDS test track was not discovered by the sequence preamble");
  }
  return *track;
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

  const SequenceDialect& dialect = ndsSequenceDialect();
  expect(dialect.usesSemanticScheduler(), "NDS SSEQ should use the semantic sequence scheduler");

  ScanIdAllocator annotationIds;
  SourceMapBuilder sourceMap([&annotationIds]() { return annotationIds.nextSourceAnnotationId(); });
  std::vector<Diagnostic> decodeDiagnostics;
  const TrackProgram track = decodeTestTrack(ByteReader(SourceId{4}, bytes), sequenceOffset, trackStart + 11,
                                             trackStart, 0, &sourceMap, &decodeDiagnostics);
  expect(track.commands.size() == 5, "NDS SSEQ dialect should decode all fixture commands");
  expect(std::ranges::all_of(track.commands, [](const SourceCommand& command) { return command.semantic(); }),
         "NDS SSEQ should store every complete command as named semantic data");
  const SourceMap annotations = sourceMap.finish();
  expect(commandDetailKind(annotations, track.commands[0]) == "nds.note-wait",
         "NDS SSEQ dialect should decode note-wait as a local command");
  expect(commandDetailKind(annotations, track.commands[1]) == "nds.note",
         "NDS SSEQ dialect should decode source note opcodes as local commands");
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
  expect(fieldEquals(fieldWithName(noteAnnotation, "key"), u64{0x3c}) &&
             fieldEquals(fieldWithName(noteAnnotation, "velocity"), u64{0x64}) &&
             fieldEquals(fieldWithName(noteAnnotation, "duration"), u64{0x18}),
         "NDS SSEQ note annotation should preserve key, velocity, and duration operands");
  expect(decodeDiagnostics.empty(), "NDS SSEQ semantic decode should not emit diagnostics for valid commands");

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
  SourceMapBuilder programSourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> programDiagnostics;
  constexpr AssetId sequenceId{7};
  const SequenceProgram decodedProgram =
      decodeNdsSequence(ByteReader(SourceId{4}, bytes), sequenceId,
                        NdsSequenceRange{.offset = sequenceOffset, .sequenceEnd = trackStart + 4}, &programSourceMap,
                        &programDiagnostics);
  expect(decodedProgram.tracks.size() == 1 && decodedProgram.tracks[0].commands.size() == 2,
         "NDS program source-link fixture should still decode program and end commands");
  const SourceMap programAnnotations = programSourceMap.finish();
  const auto* sseqHeader = annotationWithKind(programAnnotations, SourceId{4}, SourceRole::Header, "sseq-header");
  expect(sseqHeader != nullptr && sseqHeader->owner == ObjectRefs::sequence(sequenceId),
         "NDS SSEQ header annotation should point at the semantic sequence asset");
  const auto trackAnnotations = programAnnotations.withRole(SourceId{4}, SourceRole::SequenceTrack);
  expect(trackAnnotations.size() == 1, "NDS sequence parse should publish a track annotation");
  const SourceAnnotation& trackAnnotation = programAnnotations.get(trackAnnotations.front());
  expect(trackAnnotation.parent == sseqHeader->id, "NDS track annotation should be parented under the SSEQ header");
  expect(trackAnnotation.owner == ObjectRefs::sequenceTrack(sequenceId, 0),
         "NDS track annotation should point at the semantic sequence track");
  expect(trackAnnotation.range.offset == trackStart && trackAnnotation.range.size == 4,
         "NDS track annotation should span decoded command bytes");
  const auto programAnnotationIds = programAnnotations.withSequenceSemantic(SourceId{4}, SequenceSemantic::Program);
  expect(programAnnotationIds.size() == 1, "NDS program command should publish one program annotation");
  const auto& programAnnotation = programAnnotations.get(programAnnotationIds[0]);
  const auto instrumentLink = std::ranges::find_if(
      programAnnotation.links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesInstrument; });
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
  SourceMapBuilder expressionSourceMap;
  const TrackProgram expressionTrack = decodeTestTrack(ByteReader(SourceId{4}, bytes), sequenceOffset, trackStart + 3,
                                                       trackStart, 0, &expressionSourceMap);
  const SourceMap expressionAnnotations = expressionSourceMap.finish();
  expect(expressionTrack.commands.size() == 2 &&
             commandDetailKind(expressionAnnotations, expressionTrack.commands[0]) == "nds.expression",
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

  const SequenceDialect& dialect = ndsSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeTestTrack(ByteReader(SourceId{5}, bytes), sequenceOffset, subroutineOffset + 4, trackStart, 0, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 6, "NDS call fixture should decode call target and fallthrough blocks");

  const auto call = std::ranges::find_if(track.commands, [&](const SourceCommand& command) {
    return commandDetailKind(annotations, command) == "nds.call";
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
  SourceMapBuilder overlapSourceMap;
  const TrackProgram overlapTrack = decodeTestTrack(ByteReader(SourceId{8}, overlapBytes), sequenceOffset,
                                                    trackStart + 9, trackStart, 0, &overlapSourceMap);
  const SourceMap overlapAnnotations = overlapSourceMap.finish();
  expect(overlapTrack.commands.size() == 4, "NDS overlap fixture should split fallthrough from call-target bytes");
  expect(overlapTrack.commands[1].range.offset == trackStart + 4 && overlapTrack.commands[1].encodedSize == 1,
         "NDS overlap fixture should stop before overlapping a queued call target");
  const SourceAnnotation& truncated = commandAnnotation(overlapAnnotations, overlapTrack.commands[1]);
  expect(truncated.detailKind == "nds.truncated" && truncated.sequenceSemantic == SequenceSemantic::Unsupported &&
             truncated.playbackStatus == CommandPlaybackStatus::Unsupported,
         "NDS overlap fixture should annotate the command truncated at the shared block boundary");
  expect(truncated.parent && overlapAnnotations.get(*truncated.parent).role == SourceRole::SequenceTrack,
         "NDS boundary-truncated command should be parented under the recovered track annotation");
  const SequenceProgram overlapProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {overlapTrack},
  };
  const PerformanceSequence overlapPerformance = SequenceVm(LoopPolicy::PlayOnce).render(overlapProgram, dialect);
  expect(overlapPerformance.diagnostics.empty(),
         "NDS overlap fixture should render without unpaired-return diagnostics");
}

void ndsSequenceDialectDiscoversSecondaryTrackAddresses() {
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

  SourceMapBuilder sourceMap;
  const SequenceProgram program =
      decodeNdsSequence(ByteReader(SourceId{6}, bytes), AssetId{7},
                        NdsSequenceRange{.offset = sequenceOffset, .sequenceEnd = secondaryStart + 3}, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(program.tracks.size() == 2 && program.tracks[0].startAddress.value == primaryStart &&
             program.tracks[1].startAddress.value == secondaryStart,
         "NDS SSEQ preamble should discover primary and secondary track entry points");
  const TrackProgram& secondary = program.tracks[1];
  expect(secondary.sourceTrackNumber == 1 && secondary.commands.size() == 2,
         "NDS secondary track should decode independently from the primary bootstrap");
  expect(commandDetailKind(annotations, secondary.commands[0]) == "nds.rest",
         "NDS secondary track should preserve decoded source commands");
}

void ndsSequencePreambleKeepsMalformedDelayCommand() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xfe;
  bytes[trackStart + 1] = 0x00;
  bytes[trackStart + 2] = 0x00;
  bytes[trackStart + 3] = 0x80;
  bytes[trackStart + 4] = 0x81;

  SourceMapBuilder sourceMap;
  const SequenceProgram program =
      decodeNdsSequence(ByteReader(SourceId{12}, bytes), AssetId{7},
                        NdsSequenceRange{.offset = sequenceOffset, .sequenceEnd = trackStart + 5}, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(program.tracks.size() == 1 && program.tracks.front().startAddress.value == trackStart + 3,
         "NDS preamble should not skip an unterminated bootstrap variable-length command");
  const TrackProgram& track = program.tracks.front();
  expect(track.commands.size() == 1 && commandDetailKind(annotations, track.commands[0]) == "nds.truncated",
         "NDS malformed bootstrap command should be preserved as a truncated source command");
}

void ndsSequenceDialectAnnotatesIgnoredOperandBytes() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xe0;
  bytes[trackStart + 1] = 0x12;
  bytes[trackStart + 2] = 0x34;
  bytes[trackStart + 3] = 0xff;

  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeTestTrack(ByteReader(SourceId{7}, bytes), sequenceOffset, trackStart + 4, trackStart, 0, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 2, "NDS ignored-command fixture should decode the ignored command and end command");

  const SourceCommand& ignored = track.commands[0];
  expect(commandDetailKind(annotations, ignored) == "nds.modulation-delay",
         "NDS ignored opcode should stay annotated as its source-driver command");
  expect(ignored.semantic() && track.bytesFor(ignored).empty(),
         "NDS ignored command should keep its source operand without retaining playback bytes");
  const SemanticOperand* ignoredBytes = semanticOperand(ignored, "bytes");
  expect(ignoredBytes != nullptr && std::get<std::string>(ignoredBytes->value) == "12 34",
         "NDS ignored command should keep its opaque operand as a named semantic value");

  const SourceField* bytesField = fieldWithName(commandAnnotation(annotations, ignored), "bytes");
  expect(fieldEquals(bytesField, "12 34"),
         "NDS ignored command should preserve ignored operand bytes as source annotation data");
  expect(bytesField->range.offset == trackStart + 1 && bytesField->range.size == 2,
         "NDS ignored command annotation bytes should preserve their source range");
}

void ndsSequenceDialectAnnotatesPartialIgnoredOperandBytes() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xe0;
  bytes[trackStart + 1] = 0x12;

  SourceMapBuilder sourceMap;
  std::vector<Diagnostic> diagnostics;
  const TrackProgram track = decodeTestTrack(ByteReader(SourceId{15}, bytes), sequenceOffset, trackStart + 2,
                                             trackStart, 0, &sourceMap, &diagnostics);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 1, "NDS partial ignored-command fixture should decode one truncated command");
  expect(commandDetailKind(annotations, track.commands[0]) == "nds.truncated",
         "NDS partial ignored command should use the truncated-command fallback");
  expect(track.bytesFor(track.commands[0]).size() == 2 && track.bytesFor(track.commands[0])[0] == 0xe0 &&
             track.bytesFor(track.commands[0])[1] == 0x12,
         "NDS partial ignored command should preserve the opcode and available operand byte");

  const SourceField* bytesField = fieldWithName(commandAnnotation(annotations, track.commands[0]), "bytes");
  expect(fieldEquals(bytesField, "12"),
         "NDS partial ignored command should preserve available ignored operand bytes as annotation data");
  expect(bytesField->range.offset == trackStart + 1 && bytesField->range.size == 1,
         "NDS partial ignored command annotation bytes should use the partial operand range");
  expect(diagnostics.size() == 1 && diagnostics[0].message == "Truncated field 'bytes'",
         "NDS partial ignored command should diagnose the missing operand byte");
}

void ndsSequenceDialectKeepsEmptyPlaceholderTrack() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  const SequenceProgram program =
      decodeNdsSequence(ByteReader(SourceId{8}, bytes), AssetId{7},
                        NdsSequenceRange{.offset = sequenceOffset, .sequenceEnd = trackStart});
  expect(program.tracks.size() == 1 && program.tracks.front().startAddress.value == trackStart,
         "NDS empty placeholder sequences should keep their first empty track");
  expect(program.tracks.front().commands.empty(), "NDS empty placeholder tracks should not decode padding as commands");
}

void ndsSequenceDialectMarksUnterminatedVarLenAsTruncated() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0x80;
  bytes[trackStart + 1] = 0x81;

  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeTestTrack(ByteReader(SourceId{10}, bytes), sequenceOffset, trackStart + 2, trackStart, 0, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 1, "NDS unterminated variable-length command should decode as one command");
  expect(commandDetailKind(annotations, track.commands[0]) == "nds.truncated",
         "NDS unterminated variable-length command should use the truncated-command fallback");
  expect(track.bytesFor(track.commands[0]).size() == 2 && track.bytesFor(track.commands[0])[0] == 0x80 &&
             track.bytesFor(track.commands[0])[1] == 0x81,
         "NDS truncated command should preserve available partial command bytes");
}

void ndsSequenceDialectDoesNotLinkInvalidControlTargets() {
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 invalidRelativeTarget = 0x80;
  constexpr u32 sequenceEnd = trackStart + 4;
  const auto checkInvalidTarget = [&](u8 opcode, SequenceSemantic semantic, SourceLinkRole role,
                                      std::string_view detailKind, std::string_view warning) {
    std::vector<u8> bytes(0x1c0);
    bytes[trackStart + 0] = opcode;
    bytes[trackStart + 1] = static_cast<u8>(invalidRelativeTarget & 0xff);
    bytes[trackStart + 2] = static_cast<u8>((invalidRelativeTarget >> 8) & 0xff);
    bytes[trackStart + 3] = static_cast<u8>((invalidRelativeTarget >> 16) & 0xff);

    ScanIdAllocator ids;
    SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
    std::vector<Diagnostic> diagnostics;
    const TrackProgram track = decodeTestTrack(ByteReader(SourceId{14}, bytes), sequenceOffset, sequenceEnd, trackStart,
                                               0, &sourceMap, &diagnostics);

    const SourceMap annotations = sourceMap.finish();
    expect(track.commands.size() == 1 && commandDetailKind(annotations, track.commands[0]) == detailKind,
           "NDS invalid control target should preserve the source command");
    expect(diagnostics.size() == 1 && diagnostics[0].message == warning,
           "NDS invalid control target should report a decode warning");
    expect(diagnostics[0].range && diagnostics[0].range->source == SourceId{14} &&
               diagnostics[0].range->offset == trackStart && diagnostics[0].range->size == 4,
           "NDS invalid control-target diagnostic should use the command range");

    const auto commandAnnotations = annotations.withSequenceSemantic(SourceId{14}, semantic);
    expect(commandAnnotations.size() == 1, "NDS invalid control target should publish a source annotation");
    const SourceAnnotation& command = annotations.get(commandAnnotations.front());
    expect(fieldEquals(fieldWithName(command, "destination"), u64{trackStart + invalidRelativeTarget}),
           "NDS invalid control target should keep the decoded destination operand field");
    const auto link =
        std::ranges::find_if(command.links, [role](const SourceLink& sourceLink) { return sourceLink.role == role; });
    expect(link == command.links.end(), "NDS invalid control target should not publish an invalid source link");
  };

  checkInvalidTarget(0x94, SequenceSemantic::Jump, SourceLinkRole::JumpTarget, "nds.jump",
                     "Jump target outside sequence data");
  checkInvalidTarget(0x95, SequenceSemantic::Call, SourceLinkRole::CallTarget, "nds.call",
                     "Call target outside sequence data");

  std::vector<u8> truncatedBytes(trackStart + 2);
  truncatedBytes[trackStart] = 0x94;
  truncatedBytes[trackStart + 1] = 0x01;
  SourceMapBuilder truncatedSourceMap;
  const TrackProgram truncated = decodeTestTrack(ByteReader(SourceId{14}, truncatedBytes), sequenceOffset,
                                                 trackStart + 2, trackStart, 0, &truncatedSourceMap);
  const SourceMap truncatedAnnotations = truncatedSourceMap.finish();
  expect(truncated.commands.size() == 1 &&
             commandDetailKind(truncatedAnnotations, truncated.commands[0]) == "nds.truncated",
         "NDS partial control target should remain a truncated source command");
  const SourceAnnotation& truncatedCommand = commandAnnotation(truncatedAnnotations, truncated.commands[0]);
  expect(std::ranges::none_of(truncatedCommand.links,
                              [](const SourceLink& link) {
                                return link.role == SourceLinkRole::JumpTarget ||
                                       link.role == SourceLinkRole::CallTarget;
                              }),
         "NDS partial control target should not publish a link from an incomplete operand");
}

void ndsReachableDecodeKeepsExecutableJumps() {
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

  const SequenceDialect& dialect = ndsSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeTestTrack(ByteReader(SourceId{9}, bytes), sequenceOffset, subroutineOffset + 7, trackStart, 0, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  const auto jump = std::ranges::find_if(track.commands, [&](const SourceCommand& command) {
    return commandDetailKind(annotations, command) == "nds.jump";
  });
  expect(jump != track.commands.end(), "NDS reachable decode should preserve jumps as executable commands");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .behavior = SequenceProgramBehavior{.commandLimit = 64},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "NDS reachable jump loop should stop without hitting the command limit");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 2,
         "NDS reachable jump loop should render one pass through the subroutine loop");
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

  const auto bank = parseNdsInstrumentSet(
      input, AssetId{2}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())}, "Bank", out, psg, waves);
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
  expect(malformedBank.instruments.empty(), "NDS synth parser should skip regions with malformed envelope-rate bytes");

  const SourceMap annotations = out.sourceMap().finish();
  const auto* pointerTable =
      annotationWithKind(annotations, SourceId{11}, SourceRole::Table, "sbnk-instrument-pointer-table");
  expect(pointerTable != nullptr && pointerTable->range.offset == 0x38 && pointerTable->range.size == 8,
         "NDS SBNK parser should annotate the instrument pointer table");
  const auto* pointer = annotationWithKind(annotations, SourceId{11}, SourceRole::Pointer, "sbnk-instrument-pointer");
  expect(pointer != nullptr && pointer->range.offset == 0x3c && pointer->range.size == 4,
         "NDS SBNK parser should annotate instrument pointers");
  const auto* instrument = annotationWithKind(annotations, SourceId{11}, SourceRole::Instrument, "sbnk-instrument");
  expect(instrument != nullptr && instrument->range.offset == 0x40 && instrument->range.size == 10,
         "NDS SBNK parser should annotate parsed instrument rows");
  const auto sampleLink = std::ranges::find_if(
      instrument->links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesSample; });
  expect(sampleLink != instrument->links.end(), "NDS instrument annotations should link to referenced samples");
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

  ScanResultBuilder out(input, "NDS");
  const auto wave = parseNdsWaveArchive(
      input, AssetId{5}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())}, "Wave", &out);
  expect(wave.samples.samples.size() == 1, "NDS parser should keep non-looping ADPCM with loop offset zero");
  const Sample& sample = wave.samples.samples[0];
  expect(sample.encodedData.offset == 0x50 && sample.encodedData.size == 4,
         "NDS ADPCM encoded data should skip the predictor header");
  expect(!sample.loop.enabled && sample.loop.start == 0 && sample.loop.length == 9,
         "NDS non-looping ADPCM should keep sane decoded loop metadata");
  const ScanResult result = out.finish();
  const auto* waveHeader = annotationWithKind(result.sourceMap, SourceId{12}, SourceRole::Header, "swar-header");
  expect(waveHeader != nullptr && waveHeader->range.offset == 0 && waveHeader->range.size == 0x3c,
         "NDS SWAR parser should annotate the archive header");
  const auto* sampleTable =
      annotationWithKind(result.sourceMap, SourceId{12}, SourceRole::Table, "swar-sample-offset-table");
  expect(sampleTable != nullptr && sampleTable->range.offset == 0x3c && sampleTable->range.size == 4,
         "NDS SWAR parser should annotate the sample offset table");
  const auto* sampleHeader =
      annotationWithKind(result.sourceMap, SourceId{12}, SourceRole::Sample, "swar-sample-header");
  expect(sampleHeader != nullptr && sampleHeader->range.offset == 0x40 && sampleHeader->range.size == 0x0c,
         "NDS SWAR parser should annotate parsed sample headers");

  bytes[0x41] = 1;
  const auto malformedLoop = parseNdsWaveArchive(
      input, AssetId{6}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())}, "Malformed Wave");
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

  const auto wave = parseNdsWaveArchive(
      input, AssetId{7}, NdsFileRange{.offset = 0, .size = static_cast<u32>(bytes.size())}, "Truncated Wave", &out);
  expect(wave.samples.samples.empty(), "NDS parser should skip truncated SWAR sample headers");

  const ScanResult result = out.finish();
  expect(!result.diagnostics.empty(), "NDS parser should diagnose truncated SWAR sample headers");
  expect(result.diagnostics[0].message.find("SWAR sample header") != std::string::npos,
         "NDS SWAR diagnostic should name the truncated field");
}

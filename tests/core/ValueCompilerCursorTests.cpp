/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/sequence/CompilerCursor.h"

namespace {

struct CompilerProbeState {
  s8 transpose = 0;
  bool enabled = false;
};

struct CompilerProbePlayback {
  CompilerProbeState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  Effects note(u8 key, u32 duration) {
    out.note(static_cast<double>(key + track.transpose), track.enabled ? 1.0 : 0.5, duration);
    return Effects::wait(duration);
  }
};

using ProbeCursor = CompilerCursor<CompilerProbeState, CompilerProbePlayback>;
using ProbeDialect = CompiledCommandDialect<CompilerProbeState, CompilerProbePlayback>;

DecodedBytecodeCommand decodeProbeCommand(ByteReader reader, u32 begin, u32 end,
                                          std::vector<Diagnostic>* diagnostics = nullptr) {
  ProbeCursor cursor(reader, begin, end, "compiler-probe", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  switch (cursor.opcode()) {
    case 0x10: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.emitLevel(LevelScale::linearFromMidi7(event.u8("volume")));
    }
    case 0x20: {
      auto event = cursor.command("Transpose", SequenceSemantic::State);
      return event.set<&CompilerProbeState::transpose>(event.s8("semitones"));
    }
    case 0x21:
      return cursor.command("Toggle Enabled", SequenceSemantic::State).toggle<&CompilerProbeState::enabled>();
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43: {
      auto event = cursor.command("Note", SequenceSemantic::Note);
      const u8 key = event.opcodeBits<0, 2>("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      const u32 duration = event.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration);
      return event.invoke<&CompilerProbePlayback::note>(static_cast<u8>(60 + key), duration);
    }
    case 0x50: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      return event.wait(event.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration));
    }
    case 0x60: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump, CommandPlaybackStatus::AffectsControlFlow);
      return event.jump(event.address("destination"));
    }
    case 0x61: {
      auto event = cursor.command("Repeat", SequenceSemantic::Repeat, CommandPlaybackStatus::AffectsControlFlow);
      const u8 slot = event.u8("slot");
      const u32 totalPlays = event.u8("total_plays");
      const Address destination = event.address("destination");
      return event.repeatUntil(slot, totalPlays, destination);
    }
    case 0x62: {
      auto event = cursor.command("Call", SequenceSemantic::Call, CommandPlaybackStatus::AffectsControlFlow);
      return event.call(event.address("destination"));
    }
    case 0x63:
      return cursor.command("Return", SequenceSemantic::Return, CommandPlaybackStatus::AffectsControlFlow).return_();
    case 0x70: {
      auto event = cursor.sourceOnly("Conditional Fields");
      const u8 wide = event.u8("wide");
      if (wide != 0) {
        event.u16be("value");
      } else {
        event.u8("value");
      }
      return event.ignore();
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End).end();
    default: {
      auto event = cursor.unsupported("Unsupported Opcode");
      event.warning("Unsupported compiler-cursor probe opcode");
      return event.stop();
    }
  }
}

SequenceDialect compilerProbeDialect() {
  return SequenceDialect{
      .id = DialectId{.value = "compiler-probe"},
      .timebase = Timebase{.ppqn = 48},
      .createSemanticTrackState = ProbeDialect::createTrackState,
      .executeSemantic = ProbeDialect::execute,
  };
}

TrackProgram decodeProbeTrack(ByteReader reader, u32 end, SourceMapBuilder* sourceMap = nullptr,
                              std::vector<Diagnostic>* diagnostics = nullptr) {
  return decodeCompilerReachableTrack(
      reader,
      TrackDecodeInput{
          .trackIndex = 0,
          .startOffset = 0,
          .bytecodeEnd = end,
          .sourceMap = sourceMap,
          .diagnostics = diagnostics,
      },
      [=](u32 offset) { return decodeProbeCommand(reader, offset, end, diagnostics); });
}

void compilerCursorCompilesAndExecutesTypedCommands() {
  TrackProgram track;
  SourceMap sourceMap;
  {
    const std::vector<u8> bytes{0x10, 0x40, 0x20, 0x02, 0x21, 0x43, 0x04, 0x50, 0x03, 0xff};
    const ByteReader reader(SourceId{7}, bytes);
    ScanIdAllocator ids;
    SourceMapBuilder sourceMapBuilder([&ids]() { return ids.nextSourceAnnotationId(); });
    track = decodeProbeTrack(reader, static_cast<u32>(bytes.size()), &sourceMapBuilder);
    sourceMap = sourceMapBuilder.finish();
  }

  expect(track.commandBytes.empty(), "compiler-cursor commands should not retain source bytes");
  expect(track.commands.size() == 6, "compiler cursor should decode every probe command once");
  expect(track.commands[0].execution.valid() && track.commands[1].execution.valid() &&
             track.commands[2].execution.valid() && track.commands[3].execution.valid(),
         "output, state, toggle, and local handlers should compile to executor slots");

  const auto annotations = sourceMap.withRole(SourceId{7}, SourceRole::Command);
  expect(annotations.size() == 6, "compiler cursor should project one annotation per source command");
  const SourceAnnotation& volume = sourceMap.get(annotations[0]);
  expect(volume.label == "Volume" && volume.fields.size() == 2 && volume.fields[1].name == "volume" &&
             volume.fields[1].range.offset == 1,
         "field reads should automatically preserve names and exact source ranges");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "compiled probe should render without diagnostics");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 7,
         "compiled waits and local note behavior should advance VM time");
  expect(performance.tracks[0].events.size() == 2,
         "compiled probe should emit its level and note after source storage is gone");

  const auto& level = std::get<LevelPerformanceEvent>(performance.tracks[0].events[0]);
  const auto& note = std::get<NotePerformanceEvent>(performance.tracks[0].events[1]);
  expect(LevelScale::midi7FromLinear(level.linearGain) == 0x40,
         "compiled direct output should preserve its decoded value");
  expect(note.key == 65.0 && note.linearVelocity == 1.0 && note.durationTicks == 4,
         "generated member invocation should observe preceding typed track-state operations");
}

void compilerCursorCompilesControlFlow() {
  const std::vector<u8> bytes{
      0x62, 0x00, 0x08,  // call subroutine
      0x50, 0x02,        // wait
      0x60, 0x00, 0x0b,  // jump to end
      0x41, 0x01,        // subroutine note
      0x63,              // return
      0xff,              // end
  };
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{8}, bytes), static_cast<u32>(bytes.size()));
  expect(track.commands.size() == 6, "reachable decoding should compile call and jump targets");
  expect(track.commands[0].flow.callTarget() && track.commands[2].flow.unconditionalJump(),
         "compiled flow should preserve discovery targets beside runtime behavior");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 3,
         "compiled call, return, wait, and jump should execute through SequenceVm");
  expect(performance.tracks[0].events.size() == 1 &&
             std::get<NotePerformanceEvent>(performance.tracks[0].events[0]).header.tick == 0,
         "compiled call should execute its decoded subroutine before returning to fallthrough");
}

void compilerCursorCompilesRepeatsAndConditionalFields() {
  const std::vector<u8> repeatBytes{
      0x40, 0x01,              // note
      0x61, 0x00, 0x02, 0x00, 0x00,  // play twice from address zero
      0xff,
  };
  const TrackProgram track =
      decodeProbeTrack(ByteReader(SourceId{9}, repeatBytes), static_cast<u32>(repeatBytes.size()));
  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.tracks[0].endTick == 2 && performance.tracks[0].events.size() == 2,
         "compiled counted repeat should replay through shared VM state");

  const std::vector<u8> conditionalBytes{0x70, 0x01, 0x12, 0x34, 0xff};
  const TrackProgram conditional = decodeProbeTrack(ByteReader(SourceId{10}, conditionalBytes),
                                                     static_cast<u32>(conditionalBytes.size()));
  expect(conditional.commands[0].operands.size() == 2 && conditional.commands[0].encodedSize == 4,
         "imperative compiler cursor should naturally decode conditional field layouts");
}

void compilerCursorStopsTruncatedCommandsWithoutRetainingBytes() {
  const std::vector<u8> bytes{0x10};
  std::vector<Diagnostic> diagnostics;
  const TrackProgram track =
      decodeProbeTrack(ByteReader(SourceId{11}, bytes), static_cast<u32>(bytes.size()), nullptr, &diagnostics);
  expect(track.commands.size() == 1 && track.commands[0].flow.terminal,
         "truncated compiler command should become a terminal command automatically");
  expect(track.commandBytes.empty() && !track.commands[0].execution.valid(),
         "truncated compiler command should retain neither source bytes nor executable behavior");
  expect(!diagnostics.empty() && diagnostics[0].code == "truncated-record",
         "truncated compiler field should retain the shared RecordReader diagnostic");
}

}  // namespace

void runValueCompilerCursorTests() {
  compilerCursorCompilesAndExecutesTypedCommands();
  compilerCursorCompilesControlFlow();
  compilerCursorCompilesRepeatsAndConditionalFields();
  compilerCursorStopsTruncatedCommandsWithoutRetainingBytes();
}

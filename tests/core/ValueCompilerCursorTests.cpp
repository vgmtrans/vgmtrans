/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"

namespace {

struct CompilerProbeState {
  s8 transpose = 0;
  bool enabled = false;
  u8 pitchBendRange = 2;
  u8 readyDuringWaitAtTick = 0;
};

struct CompilerProbeProgramState {
  u32 executedCommands = 0;
};

struct CompilerPrepassProgramState : CompilerProbeProgramState {
  void finishPrepass() {}
};

struct CompilerProbePlayback {
  CompilerProbeState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  CompilerProbeProgramState& program;

  void beforeCommand() { ++program.executedCommands; }

  Effects note(u8 key, u32 duration) {
    out.note(static_cast<double>(key + track.transpose), track.enabled ? 1.0 : 0.5, duration);
    return Effects::wait(duration);
  }

  [[nodiscard]] bool readyDuringWait() const { return vm.tick() >= track.readyDuringWaitAtTick; }

  void duringWaitExpression(u8 value) { out.expression(value / 127.0); }

  void pitchBend(s8 encoded) { out.pitchBend((encoded / 128.0) * track.pitchBendRange); }

  void enabledExpression(bool enabled) { out.expression(enabled ? 0.75 : 0.25); }
};

using ProbeCursor = CompilerCursor<CompilerProbeState, CompilerProbePlayback>;

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
    case 0x22: {
      auto event = cursor.command("Pitch Bend Range", SequenceSemantic::Pitch);
      const u8 semitones = event.u8("semitones");
      return event.set<&CompilerProbeState::pitchBendRange>(semitones).emitPitchBendRange(semitones);
    }
    case 0x23: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      return event.invoke<&CompilerProbePlayback::pitchBend>(event.s8("fraction"));
    }
    case 0x24: {
      auto event = cursor.command("Separate Actions", SequenceSemantic::State);
      event.set<&CompilerProbeState::transpose>(event.s8("semitones"));
      event.emitExpression(0.5);
      return event.wait(3);
    }
    case 0x25: {
      auto event = cursor.command("Inline Handler", SequenceSemantic::Pan);
      const u8 pan = event.u8("pan");
      return event.invoke([pan](CompilerProbePlayback& playback) { playback.out.pan((pan / 63.5) - 1.0); });
    }
    case 0x26: {
      auto event = cursor.command("Conflicting Flow", SequenceSemantic::State);
      return event.invokeFlow([](CompilerProbePlayback& playback) { return playback.vm.end(); })
          .invoke([](CompilerProbePlayback& playback) { return playback.vm.return_(); });
    }
    case 0x27: {
      auto event = cursor.command("Runtime State", SequenceSemantic::State);
      return event.invoke([](CompilerProbePlayback& playback) {
        playback.track.enabled = !playback.track.enabled;
        playback.out.legatoPedal(playback.track.enabled);
        playback.enabledExpression(playback.track.enabled);
      });
    }
    case 0x28:
      return cursor.sourceOnly("Promoted Action").set<&CompilerProbeState::enabled>(true);
    case 0x29: {
      auto event = cursor.sourceOnly("Ignored Action");
      event.set<&CompilerProbeState::enabled>(false);
      return event.ignore();
    }
    case 0x2a: {
      auto event = cursor.command("Wait Readiness", SequenceSemantic::State);
      return event.set<&CompilerProbeState::readyDuringWaitAtTick>(event.u8("tick"));
    }
    case 0x2b: {
      auto event = cursor.command("During-Wait Expression", SequenceSemantic::State);
      return event.invoke<&CompilerProbePlayback::duringWaitExpression>(event.u8("value"))
          .duringWaitWhen<&CompilerProbePlayback::readyDuringWait>();
    }
    case 0x2c: {
      auto event = cursor.command("Invalid During-Wait Wait", SequenceSemantic::State);
      event.set<&CompilerProbeState::readyDuringWaitAtTick>(1);
      return event.wait<&CompilerProbeState::readyDuringWaitAtTick>()
          .duringWaitWhen<&CompilerProbePlayback::readyDuringWait>();
    }
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
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.jump(event.address("destination", SemanticOperandRole::JumpTarget));
    }
    case 0x61: {
      auto event = cursor.command("Repeat", SequenceSemantic::Repeat);
      const u8 slot = event.u8("slot");
      const u32 totalPlays = event.u8("total_plays");
      const Address destination = event.address("destination", SemanticOperandRole::RepeatTarget);
      return event.repeatUntil(slot, totalPlays, destination);
    }
    case 0x62: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      return event.call(event.address("destination", SemanticOperandRole::CallTarget));
    }
    case 0x63:
      return cursor.command("Return", SequenceSemantic::Return).return_();
    case 0x64: {
      auto event = cursor.command("Equal-Valued Target", SequenceSemantic::Jump);
      const Address destination = event.address("destination", SemanticOperandRole::JumpTarget);
      event.u16be("count", SourceValueDisplay::Default, SemanticOperandRole::Count);
      return event.jump(destination);
    }
    case 0x69: {
      auto event = cursor.command("Duplicate Static Flow", SequenceSemantic::Jump);
      const Address jumpDestination = event.address("jump_destination", SemanticOperandRole::JumpTarget);
      const Address callDestination = event.address("call_destination", SemanticOperandRole::CallTarget);
      event.jump(jumpDestination);
      return event.call(callDestination);
    }
    case 0x6a: {
      auto event = cursor.command("Return Boundary Before Jump", SequenceSemantic::Jump);
      const Address destination = event.address("destination", SemanticOperandRole::JumpTarget);
      event.discoverReturn();
      return event.jump(destination);
    }
    case 0x6b: {
      auto event = cursor.command("Return Boundary After Jump", SequenceSemantic::Jump);
      const Address destination = event.address("destination", SemanticOperandRole::JumpTarget);
      event.jump(destination);
      return event.discoverReturn();
    }
    case 0x70: {
      auto event = cursor.sourceOnly("Conditional Fields");
      const u8 wide = event.u8("wide");
      if (wide != 0) {
        event.u16be("value");
      } else {
        event.u8("value");
      }
      return event;
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
      .timebase = Timebase{.ppqn = 48},
      .behavior =
          SequenceProgramBehavior{
              .panLaw = PanLaw::EqualPower,
          },
  };
}

SequenceRuntime compilerProbeRuntime() {
  return makeCompiledRuntime<ProbeCursor, CompilerProbeProgramState>();
}

TrackProgram decodeProbeTrack(ByteReader reader, u32 end, SourceMapBuilder* sourceMap = nullptr,
                              std::vector<Diagnostic>* diagnostics = nullptr) {
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
      .sourceMap = sourceMap,
  };
  return tracks.decode(0, 0, [=](u32 offset) { return decodeProbeCommand(reader, offset, end, diagnostics); });
}

void compilerCursorCompilesAndExecutesTypedCommands() {
  TrackProgram track;
  SourceMap sourceMap;
  {
    const std::vector<u8> bytes{0x10, 0x40, 0x20, 0x02, 0x21, 0x43, 0x04, 0x50, 0x03, 0x28, 0x29, 0xff};
    const ByteReader reader(SourceId{7}, bytes);
    ScanIdAllocator ids;
    SourceMapBuilder sourceMapBuilder([&ids]() { return ids.nextSourceAnnotationId(); });
    track = decodeProbeTrack(reader, static_cast<u32>(bytes.size()), &sourceMapBuilder);
    sourceMap = sourceMapBuilder.finish();
  }

  expect(std::ranges::all_of(track.commands, [](const SourceCommand& command) { return command.range.size != 0; }),
         "compiler-cursor commands should retain source ranges instead of source bytes");
  expect(track.commands.size() == 8, "compiler cursor should decode every probe command once");
  expect(track.commands[0].execution.valid() && track.commands[1].execution.valid() &&
             track.commands[2].execution.valid() && track.commands[3].execution.valid(),
         "output, state, toggle, and local handlers should compile to command bodies");

  const auto annotations = sourceMap.withRole(SourceId{7}, SourceRole::Command);
  expect(annotations.size() == 8, "compiler cursor should project one annotation per source command");
  const SourceAnnotation& volume = sourceMap.get(annotations[0]);
  expect(volume.label == "Volume" && volume.fields.size() == 2 && volume.fields[1].name == "volume" &&
             volume.fields[1].range.offset == 1,
         "field reads should automatically preserve names and exact source ranges");
  expect(sourceMap.get(annotations[5]).playbackStatus == CommandPlaybackStatus::AffectsPlayback &&
             sourceMap.get(annotations[6]).playbackStatus == CommandPlaybackStatus::SourceOnly,
         "compiled behavior should promote source-only presentation unless the event is explicitly ignored");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .runtime = compilerProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
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
  SourceMapBuilder sourceMapBuilder;
  const TrackProgram track =
      decodeProbeTrack(ByteReader(SourceId{8}, bytes), static_cast<u32>(bytes.size()), &sourceMapBuilder);
  const SourceMap sourceMap = sourceMapBuilder.finish();
  expect(track.commands.size() == 6, "reachable decoding should compile call and jump targets");
  expect(track.commands[0].flow.callTarget() && track.commands[2].flow.unconditionalJump(),
         "compiled flow should preserve discovery targets beside runtime behavior");
  expect(!track.commands[0].execution.valid() && !track.commands[2].execution.valid() &&
             !track.commands[4].execution.valid() && !track.commands[5].execution.valid(),
         "static call, jump, return, and end flow should compile no redundant runtime bodies");
  const SourceAnnotation& callAnnotation = sourceMap.get(track.commands[0].annotation);
  const SourceAnnotation& jumpAnnotation = sourceMap.get(track.commands[2].annotation);
  const SourceAnnotation& returnAnnotation = sourceMap.get(track.commands[4].annotation);
  expect(callAnnotation.playbackStatus == CommandPlaybackStatus::AffectsControlFlow &&
             jumpAnnotation.playbackStatus == CommandPlaybackStatus::AffectsControlFlow &&
             returnAnnotation.playbackStatus == CommandPlaybackStatus::AffectsControlFlow,
         "compiler-cursor flow operations should annotate their playback status automatically");
  const SemanticOperand* callDestination = semanticOperand(track.commands[0], "destination");
  const SemanticOperand* jumpDestination = semanticOperand(track.commands[2], "destination");
  expect(callDestination != nullptr && callDestination->role == SemanticOperandRole::CallTarget &&
             jumpDestination != nullptr && jumpDestination->role == SemanticOperandRole::JumpTarget,
         "target roles declared at operand creation should remain attached to the exact operands");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .runtime = compilerProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 3,
         "compiled call, return, wait, and jump should execute through SequenceVm");
  expect(performance.tracks[0].events.size() == 1 &&
             std::get<NotePerformanceEvent>(performance.tracks[0].events[0]).header.tick == 0,
         "compiled call should execute its decoded subroutine before returning to fallthrough");
}

void compilerCursorCompilesRepeatsAndConditionalFields() {
  const std::vector<u8> repeatBytes{
      0x40, 0x01,                    // note
      0x61, 0x00, 0x02, 0x00, 0x00,  // play twice from address zero
      0xff,
  };
  const TrackProgram track =
      decodeProbeTrack(ByteReader(SourceId{9}, repeatBytes), static_cast<u32>(repeatBytes.size()));
  const SemanticOperand* repeatDestination = semanticOperand(track.commands[1], "destination");
  expect(repeatDestination != nullptr && repeatDestination->role == SemanticOperandRole::RepeatTarget,
         "compiled repeats should annotate their destination without a duplicate read-time role");
  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .runtime = compilerProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.tracks[0].endTick == 2 && performance.tracks[0].events.size() == 2,
         "compiled counted repeat should replay through shared VM state");

  const std::vector<u8> conditionalBytes{0x70, 0x01, 0x12, 0x34, 0xff};
  const TrackProgram conditional =
      decodeProbeTrack(ByteReader(SourceId{10}, conditionalBytes), static_cast<u32>(conditionalBytes.size()));
  expect(conditional.commands[0].operands.size() == 2 && conditional.commands[0].range.size == 4,
         "imperative compiler cursor should naturally decode conditional field layouts");
}

void compilerCursorComposesOperationsIntoOneBody() {
  const std::vector<u8> bytes{
      0x22, 0x0c,  // set and emit pitch-bend range
      0x23, 0x40,  // bend halfway across that range
      0x24, 0x03,  // separate state, expression, and wait operations
      0x40, 0x01,  // note after the wait, using the new transpose
      0x25, 0x7f,  // value-capturing inline body
      0xff,
  };
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{12}, bytes), static_cast<u32>(bytes.size()));
  expect(track.commands.size() == 6 && track.commands[0].execution.valid() && track.commands[2].execution.valid(),
         "chained and separate compiler-cursor calls should compose into one command body");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .runtime = compilerProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 4,
         "composed operations should execute through one source command before VM scheduling continues");
  const auto& events = performance.tracks[0].events;
  expect(events.size() == 5 && std::get<PitchBendRangePerformanceEvent>(events[0]).cents == 1200 &&
             std::get<PitchBendPerformanceEvent>(events[1]).semitones == 6.0 &&
             std::get<ExpressionPerformanceEvent>(events[2]).linearGain == 0.5,
         "composed state and output operations should execute in their written order");
  expect(std::get<NotePerformanceEvent>(events[3]).header.tick == 3 &&
             std::get<NotePerformanceEvent>(events[3]).key == 63.0 &&
             std::get<PanPerformanceEvent>(events[4]).stereoPosition == 1.0,
         "separate state calls and a value-capturing body should preserve typed runtime behavior");
}

void compilerCursorReadsRuntimeStateInsideCommandBody() {
  const std::vector<u8> bytes{0x27, 0x27, 0xff};
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{14}, bytes), static_cast<u32>(bytes.size()));
  expect(track.commands.size() == 3 && track.commands[0].execution.valid(),
         "runtime-state fixture should compile its related effects into one body");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .runtime = compilerProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  const auto& events = performance.tracks[0].events;
  expect(events.size() == 4 && std::get<LegatoPedalPerformanceEvent>(events[0]).enabled &&
             !std::get<LegatoPedalPerformanceEvent>(events[2]).enabled,
         "a command body should observe the track state immediately after updating it");
  expect(std::get<ExpressionPerformanceEvent>(events[1]).linearGain == 0.75 &&
             std::get<ExpressionPerformanceEvent>(events[3]).linearGain == 0.25,
         "a local playback operation should select output from runtime state");
}

void compilerCursorExecutesEligibleCommandsDuringWaits() {
  const auto render = [](std::initializer_list<u8> bytes) {
    const std::vector<u8> source(bytes);
    const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{16}, source), static_cast<u32>(source.size()));
    const SequenceDialect dialect = compilerProbeDialect();
    const SequenceProgram program{
        .runtime = compilerProbeRuntime(),
        .timebase = dialect.timebase,
        .behavior = dialect.behavior,
        .tracks = {track},
    };
    return std::pair{track, SequenceVm().render(program)};
  };

  const auto [gatedTrack, gated] = render({0x2a, 0x02, 0x50, 0x04, 0x2b, 0x20, 0x2b, 0x40, 0xff});
  expect(gatedTrack.commands.size() == 5 && gatedTrack.commands[2].execution.duringWait &&
             gatedTrack.commands[3].execution.duringWait,
         "during-wait eligibility should remain attached to each independently decoded command");
  const auto& gatedEvents = gated.tracks[0].events;
  expect(gated.tracks[0].endTick == 4 && gatedEvents.size() == 2 &&
             std::get<ExpressionPerformanceEvent>(gatedEvents[0]).header.tick == 2 &&
             std::get<ExpressionPerformanceEvent>(gatedEvents[1]).header.tick == 3,
         "the VM should retain a gated command and execute at most one eligible command per wait tick");

  const auto [boundaryTrack, boundary] = render({0x2a, 0x00, 0x50, 0x02, 0x2b, 0x10, 0x2b, 0x20, 0x2b, 0x30, 0xff});
  const auto& boundaryEvents = boundary.tracks[0].events;
  expect(boundaryTrack.commands.size() == 6 && boundary.tracks[0].endTick == 2 && boundaryEvents.size() == 3 &&
             std::get<ExpressionPerformanceEvent>(boundaryEvents[0]).header.tick == 0 &&
             std::get<ExpressionPerformanceEvent>(boundaryEvents[1]).header.tick == 1 &&
             std::get<ExpressionPerformanceEvent>(boundaryEvents[2]).header.tick == 2,
         "the VM should poll once when a wait begins and resume ordinary command execution at its final boundary");

  bool rejectedWait = false;
  try {
    static_cast<void>(render({0x2a, 0x00, 0x50, 0x02, 0x2c, 0xff}));
  } catch (const std::logic_error&) {
    rejectedWait = true;
  }
  expect(rejectedWait, "a command executed during another command's wait must remain a zero-time operation");
}

void compilerCursorStopsTruncatedCommandsWithoutExecutableBehavior() {
  const std::vector<u8> bytes{0x10};
  std::vector<Diagnostic> diagnostics;
  const TrackProgram track =
      decodeProbeTrack(ByteReader(SourceId{11}, bytes), static_cast<u32>(bytes.size()), nullptr, &diagnostics);
  expect(track.commands.size() == 1 && track.commands[0].flow.endsPlayback(),
         "truncated compiler command should become a terminal command automatically");
  expect(track.commands[0].range.size == 1 && !track.commands[0].execution.valid(),
         "truncated compiler command should retain its partial source range but no executable behavior");
  expect(!diagnostics.empty() && diagnostics[0].code == "truncated-record",
         "truncated compiler field should retain the shared RecordReader diagnostic");
}

void compilerCursorKeepsExactTargetOperandRoles() {
  const std::vector<u8> bytes{0x64, 0x12, 0x34, 0x12, 0x34};
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{17}, bytes), static_cast<u32>(bytes.size()));
  expect(track.commands.size() == 1 && track.commands[0].operands.size() == 2,
         "equal-valued target fixture should decode both operands");
  expect(track.commands[0].operands[0].role == SemanticOperandRole::JumpTarget &&
             track.commands[0].operands[1].role == SemanticOperandRole::Count,
         "flow declaration must not relabel a different operand with the same numeric value");
}

void compilerCursorRejectsConflictingDefaultFlowDeclarations() {
  const auto decode = [](std::initializer_list<u8> source) {
    const std::vector<u8> bytes(source);
    return decodeProbeCommand(ByteReader(SourceId{18}, bytes), 0, static_cast<u32>(bytes.size()));
  };

  const auto rejects = [&](std::initializer_list<u8> source) {
    try {
      static_cast<void>(decode(source));
      return false;
    } catch (const std::logic_error&) {
      return true;
    }
  };
  expect(rejects({0x69, 0x00, 0x01, 0x00, 0x02}), "a command should reject a second default transition");
  expect(rejects({0x6a, 0x00, 0x01}) && rejects({0x6b, 0x00, 0x01}),
         "a discover-return command should conflict with another default transition in either declaration order");
}

void compilerCursorRejectsConflictingComposedFlow() {
  const std::vector<u8> bytes{0x26, 0xff};
  ScanIdAllocator ids;
  SourceMapBuilder sourceMapBuilder([&ids]() { return ids.nextSourceAnnotationId(); });
  const TrackProgram track =
      decodeProbeTrack(ByteReader(SourceId{13}, bytes), static_cast<u32>(bytes.size()), &sourceMapBuilder);
  const SourceMap sourceMap = sourceMapBuilder.finish();
  expect(sourceMap.get(track.commands[0].annotation).playbackStatus == CommandPlaybackStatus::AffectsControlFlow,
         "runtime-selected flow should be classified as control flow without a separate annotation call");
  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .runtime = compilerProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  bool rejected = false;
  try {
    static_cast<void>(SequenceVm().render(program));
  } catch (const std::logic_error&) {
    rejected = true;
  }
  expect(rejected, "one compiled source command should not produce multiple control-flow results");
}

void compilerCursorAnalysisStopsAfterItsScheduledPrepass() {
  const std::vector<u8> bytes{0x21, 0xff};
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{15}, bytes), static_cast<u32>(bytes.size()));
  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .runtime = makeCompiledRuntime<ProbeCursor, CompilerPrepassProgramState>(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const u32 executed =
      analyzeCompiledProgram<CompilerPrepassProgramState>(program, &CompilerPrepassProgramState::executedCommands);
  expect(executed == 2, "compiled analysis should not execute a discarded output pass after its scheduled prepass");
}

void trackDecodeSessionOrdersExceptionalWalkerCommands() {
  const std::vector<u8> bytes{0x40, 0x01, 0xff};
  const u32 end = static_cast<u32>(bytes.size());
  const ByteReader reader(SourceId{30}, bytes);
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
  };
  auto session = tracks.begin(0, 0);
  session.append(decodeProbeCommand(reader, 2, end), 2);
  session.append(decodeProbeCommand(reader, 0, end), 0);
  const TrackProgram track = session.finish();

  expect(track.commands.size() == 2 && track.commands[0].address.value == 0 &&
             track.commands[1].address.value == 2,
         "track decode session should order exceptional-walker commands by source address");

  const SequenceDialect dialect = compilerProbeDialect();
  const PerformanceSequence performance = SequenceVm().render(SequenceProgram{
      .runtime = compilerProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  });
  expect(performance.diagnostics.empty() && performance.tracks[0].events.size() == 1 &&
             performance.tracks[0].endTick == 1,
         "ordered exceptional-walker commands should retain their decoded execution flow");
}

void trackDecodeSourceHierarchyDistinguishesTrackedAndTracklessFormats() {
  const std::vector<u8> bytes{0xff};

  SourceMapBuilder trackedSourceMap;
  const AssetId trackedAsset{31};
  const TrackDecodeScope tracked{
      .reader = ByteReader(SourceId{31}, bytes),
      .sequenceAsset = trackedAsset,
      .sourceMap = &trackedSourceMap,
  };
  const TrackProgram trackedProgram =
      tracked.decode(0, 0, [&](u32 offset) { return decodeProbeCommand(tracked.reader, offset, 1); });
  const SourceMap trackedAnnotations = trackedSourceMap.finish();
  const auto sourceTracks = trackedAnnotations.withRole(SourceId{31}, SourceRole::SequenceTrack);
  const auto* trackedCommand = trackedAnnotations.find(trackedProgram.commands.front().annotation);
  expect(sourceTracks.size() == 1 && trackedCommand != nullptr && trackedCommand->parent == sourceTracks.front() &&
             trackedAnnotations.assetOwner(trackedCommand->id) == trackedAsset,
         "tracked decoding should retain its source-track parent and inherited sequence ownership");

  SourceMapBuilder tracklessSourceMap;
  const AssetId tracklessAsset{32};
  const TrackDecodeScope trackless{
      .reader = ByteReader(SourceId{32}, bytes),
      .sourceHasTracks = false,
      .sequenceAsset = tracklessAsset,
      .sourceMap = &tracklessSourceMap,
  };
  const TrackProgram tracklessProgram =
      trackless.decode(0, 0, [&](u32 offset) { return decodeProbeCommand(trackless.reader, offset, 1); });
  const SourceMap tracklessAnnotations = tracklessSourceMap.finish();
  const auto* rootCommand = tracklessAnnotations.find(tracklessProgram.commands.front().annotation);
  expect(tracklessAnnotations.withRole(SourceId{32}, SourceRole::SequenceTrack).empty() && rootCommand != nullptr &&
             !rootCommand->parent && rootCommand->owner == ObjectRefs::sequence(tracklessAsset),
         "trackless decoding should publish sequence-owned commands directly at the source root");
}

}  // namespace

void runValueCompilerCursorTests() {
  compilerCursorCompilesAndExecutesTypedCommands();
  compilerCursorCompilesControlFlow();
  compilerCursorCompilesRepeatsAndConditionalFields();
  compilerCursorComposesOperationsIntoOneBody();
  compilerCursorReadsRuntimeStateInsideCommandBody();
  compilerCursorExecutesEligibleCommandsDuringWaits();
  compilerCursorStopsTruncatedCommandsWithoutExecutableBehavior();
  compilerCursorKeepsExactTargetOperandRoles();
  compilerCursorRejectsConflictingDefaultFlowDeclarations();
  compilerCursorRejectsConflictingComposedFlow();
  compilerCursorAnalysisStopsAfterItsScheduledPrepass();
  trackDecodeSessionOrdersExceptionalWalkerCommands();
  trackDecodeSourceHierarchyDistinguishesTrackedAndTracklessFormats();
}

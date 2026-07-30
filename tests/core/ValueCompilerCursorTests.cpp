/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"

namespace {

struct CompilerProbeState {
  s8 transpose = 0;
  bool enabled = false;
  u8 pitchBendRange = 2;
  u8 readyDuringWaitAtTick = 0;
};

struct CompilerProbeProgramState {
  u32 executedCommands = 0;

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

  void deferredExpression(u8 value) { out.expression(value / 127.0); }

  void pitchBend(s8 encoded) { out.pitchBend((encoded / 128.0) * track.pitchBendRange); }

  void enabledExpression(bool enabled) { out.expression(enabled ? 0.75 : 0.25); }

  [[nodiscard]] Effects deferredWait() { return Effects::wait(1); }
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
      return event.invoke([](CompilerProbePlayback& playback, u8 pan) { playback.out.pan((pan / 63.5) - 1.0); },
                          event.u8("pan"));
    }
    case 0x26: {
      auto event = cursor.command("Conflicting Flow", SequenceSemantic::State);
      return event.invoke([](CompilerProbePlayback&) { return Effects::overrideWith(Step::end()); })
          .invoke([](CompilerProbePlayback&) { return Effects::overrideWith(Step::return_()); })
          .runtimeControlFlow();
    }
    case 0x27: {
      auto event = cursor.command("Deferred State", SequenceSemantic::State);
      const auto enabled = event.state<&CompilerProbeState::enabled>();
      event.toggle<&CompilerProbeState::enabled>();
      event.emitLegatoPedal(enabled);
      return event.invoke<&CompilerProbePlayback::enabledExpression>(enabled);
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
      return event.invoke<&CompilerProbePlayback::deferredExpression>(event.u8("value"))
          .duringWaitWhen<&CompilerProbePlayback::readyDuringWait>();
    }
    case 0x2c:
      return cursor.command("Invalid During-Wait Wait", SequenceSemantic::State)
          .invoke<&CompilerProbePlayback::deferredWait>()
          .duringWaitWhen<&CompilerProbePlayback::readyDuringWait>();
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
      event.u16be("count", SourceValueDisplay::Default, SemanticOperandRole::Count);
      return event.jump(event.address("destination", SemanticOperandRole::JumpTarget));
    }
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
  return makeCompiledDialect<CompilerProbeState, CompilerProbePlayback, CompilerProbeProgramState>(SequenceDialect{
      .id = DialectId{.value = "compiler-probe"},
      .timebase = Timebase{.ppqn = 48},
      .defaultBehavior =
          SequenceProgramBehavior{
              .panLaw = PanLaw::EqualPower,
          },
  });
}

u32 projectExecutedCommands(const CompilerProbeProgramState& state) {
  return state.executedCommands;
}

TrackProgram decodeProbeTrack(ByteReader reader, u32 end, SourceMapBuilder* sourceMap = nullptr,
                              std::vector<Diagnostic>* diagnostics = nullptr) {
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
      .sourceMap = sourceMap,
  };
  return tracks.reachable(0, 0, [=](u32 offset) { return decodeProbeCommand(reader, offset, end, diagnostics); });
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

  expect(std::ranges::all_of(track.commands, [](const SourceCommand& command) { return command.encodedSize != 0; }),
         "compiler-cursor commands should retain source ranges instead of source bytes");
  expect(track.commands.size() == 8, "compiler cursor should decode every probe command once");
  expect(track.commands[0].execution.valid() && track.commands[1].execution.valid() &&
             track.commands[2].execution.valid() && track.commands[3].execution.valid(),
         "output, state, toggle, and local handlers should compile to direct executors");

  const auto annotations = sourceMap.withRole(SourceId{7}, SourceRole::Command);
  expect(annotations.size() == 8, "compiler cursor should project one annotation per source command");
  const SourceAnnotation& volume = sourceMap.get(annotations[0]);
  expect(volume.label == "Volume" && volume.fields.size() == 2 && volume.fields[1].name == "volume" &&
             volume.fields[1].range.offset == 1,
         "field reads should automatically preserve names and exact source ranges");
  expect(sourceMap.get(annotations[5]).playbackStatus == CommandPlaybackStatus::AffectsPlayback &&
             sourceMap.get(annotations[6]).playbackStatus == CommandPlaybackStatus::SourceOnly,
         "compiled actions should promote source-only presentation unless the event is explicitly ignored");

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
  SourceMapBuilder sourceMapBuilder;
  const TrackProgram track =
      decodeProbeTrack(ByteReader(SourceId{8}, bytes), static_cast<u32>(bytes.size()), &sourceMapBuilder);
  const SourceMap sourceMap = sourceMapBuilder.finish();
  expect(track.commands.size() == 6, "reachable decoding should compile call and jump targets");
  expect(track.commands[0].flow.callTarget() && track.commands[2].flow.unconditionalJump(),
         "compiled flow should preserve discovery targets beside runtime behavior");
  expect(track.commands[0].execution.actions.empty() && track.commands[2].execution.actions.empty() &&
             track.commands[4].execution.actions.empty() && track.commands[5].execution.actions.empty(),
         "static call, jump, return, and end flow should compile no redundant runtime actions");
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
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.tracks[0].endTick == 2 && performance.tracks[0].events.size() == 2,
         "compiled counted repeat should replay through shared VM state");

  const std::vector<u8> conditionalBytes{0x70, 0x01, 0x12, 0x34, 0xff};
  const TrackProgram conditional =
      decodeProbeTrack(ByteReader(SourceId{10}, conditionalBytes), static_cast<u32>(conditionalBytes.size()));
  expect(conditional.commands[0].operands.size() == 2 && conditional.commands[0].encodedSize == 4,
         "imperative compiler cursor should naturally decode conditional field layouts");
}

void compilerCursorComposesChainedAndSeparateActions() {
  const std::vector<u8> bytes{
      0x22, 0x0c,  // set and emit pitch-bend range
      0x23, 0x40,  // bend halfway across that range
      0x24, 0x03,  // separate state, expression, and wait actions
      0x40, 0x01,  // note after the wait, using the new transpose
      0x25, 0x7f,  // captureless inline handler
      0xff,
  };
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{12}, bytes), static_cast<u32>(bytes.size()));
  expect(track.commands.size() == 6 && track.commands[0].execution.actions.size() == 2 &&
             track.commands[2].execution.actions.size() == 3,
         "chained and separate compiler-cursor calls should retain the same ordered action list");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 4,
         "composed actions should execute through one source command before VM scheduling continues");
  const auto& events = performance.tracks[0].events;
  expect(events.size() == 5 && std::get<PitchBendRangePerformanceEvent>(events[0]).cents == 1200 &&
             std::get<PitchBendPerformanceEvent>(events[1]).semitones == 6.0 &&
             std::get<ExpressionPerformanceEvent>(events[2]).linearGain == 0.5,
         "composed state and output actions should execute in their written order");
  expect(std::get<NotePerformanceEvent>(events[3]).header.tick == 3 &&
             std::get<NotePerformanceEvent>(events[3]).key == 63.0 &&
             std::get<PanPerformanceEvent>(events[4]).stereoPosition == 1.0,
         "separate state calls and captureless inline handlers should preserve typed runtime behavior");
}

void compilerCursorEvaluatesDeferredStateInActionOrder() {
  const std::vector<u8> bytes{0x27, 0x27, 0xff};
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{14}, bytes), static_cast<u32>(bytes.size()));
  expect(track.commands.size() == 3 && track.commands[0].execution.actions.size() == 3,
         "deferred-state fixture should compile toggle and both outputs as separate actions");
  expect(track.commands[0].execution.actions[1].arguments.empty() &&
             track.commands[0].execution.actions[2].arguments.empty(),
         "deferred state-member identities should require no durable arguments");

  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  const auto& events = performance.tracks[0].events;
  expect(events.size() == 4 && std::get<LegatoPedalPerformanceEvent>(events[0]).enabled &&
             !std::get<LegatoPedalPerformanceEvent>(events[2]).enabled,
         "state() should observe each toggle immediately before the dependent output action");
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
        .dialect = dialect.id,
        .timebase = dialect.timebase,
        .tracks = {track},
    };
    return std::pair{track, SequenceVm().render(program, dialect)};
  };

  const auto [gatedTrack, gated] = render({0x2a, 0x02, 0x50, 0x04, 0x2b, 0x20, 0x2b, 0x40, 0xff});
  expect(gatedTrack.commands.size() == 5 && gatedTrack.commands[2].execution.duringWait.valid() &&
             gatedTrack.commands[3].execution.duringWait.valid(),
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
  expect(track.commands[0].operands[0].role == SemanticOperandRole::Count &&
             track.commands[0].operands[1].role == SemanticOperandRole::JumpTarget,
         "flow declaration must not relabel a different operand with the same numeric value");
}

void compilerCursorRejectsConflictingComposedFlow() {
  const std::vector<u8> bytes{0x26, 0xff};
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{13}, bytes), static_cast<u32>(bytes.size()));
  const SequenceDialect dialect = compilerProbeDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  bool rejected = false;
  try {
    static_cast<void>(SequenceVm().render(program, dialect));
  } catch (const std::logic_error&) {
    rejected = true;
  }
  expect(rejected, "one compiled source command should not produce multiple control-flow results");
}

void compilerCursorAnalysisStopsAfterItsScheduledPrepass() {
  const std::vector<u8> bytes{0x21, 0xff};
  const TrackProgram track = decodeProbeTrack(ByteReader(SourceId{15}, bytes), static_cast<u32>(bytes.size()));
  SequenceDialect dialect = compilerProbeDialect();
  dialect.prepass = SemanticPrepassMode::ScheduledPlayback;
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const u32 executed =
      analyzeCompiledProgram<CompilerProbeProgramState, u32>(program, dialect, projectExecutedCommands);
  expect(executed == 2, "compiled analysis should not execute a discarded output pass after its scheduled prepass");
}

}  // namespace

void runValueCompilerCursorTests() {
  compilerCursorCompilesAndExecutesTypedCommands();
  compilerCursorCompilesControlFlow();
  compilerCursorCompilesRepeatsAndConditionalFields();
  compilerCursorComposesChainedAndSeparateActions();
  compilerCursorEvaluatesDeferredStateInActionOrder();
  compilerCursorExecutesEligibleCommandsDuringWaits();
  compilerCursorStopsTruncatedCommandsWithoutExecutableBehavior();
  compilerCursorKeepsExactTargetOperandRoles();
  compilerCursorRejectsConflictingComposedFlow();
  compilerCursorAnalysisStopsAfterItsScheduledPrepass();
}

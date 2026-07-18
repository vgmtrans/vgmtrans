/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

namespace {

void sequenceVmExecutesSourceCommandsAndStopsAtPlayOnceLoop() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{2},
      .sourceTrackNumber = 7,
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  const CommandId noteCommandId =
      addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes).id;
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(),
         "sequence VM should render the probe sequence without diagnostics" +
             (performance.diagnostics.empty() ? std::string{} : ": " + performance.diagnostics.front().message));
  expect(performance.tracks.size() == 1, "sequence VM should render one performance track");
  const PerformanceTrack& renderedTrack = performance.tracks[0];
  expect(renderedTrack.id == TrackId{2} && renderedTrack.sourceTrackNumber == 7,
         "performance track should preserve source track identity");
  expect(renderedTrack.endTick == 12, "default play-once loop policy should stop at the first repeated command");
  expect(renderedTrack.events.size() == 2, "VM should emit program and note events before the loop repeats");

  const auto* instrument = std::get_if<InstrumentPerformanceEvent>(&renderedTrack.events[0]);
  expect(instrument != nullptr && instrument->program == 5,
         "program command should emit a target-neutral instrument event");
  expect(instrument->header.sourceCommand == CommandId{0} && instrument->header.tick == 0,
         "instrument event should link to the source command and tick");

  const auto* note = std::get_if<NotePerformanceEvent>(&renderedTrack.events[1]);
  expect(note != nullptr, "note command should emit a target-neutral note event");
  expect(note->key == 64.0 && note->linearVelocity == 0.5 && note->durationTicks == 12,
         "note event should use driver state and dialect context while staying MIDI-neutral");
  expect(note->header.sourceCommand == noteCommandId && note->header.tick == 0,
         "note event should link back to the source command that emitted it");
  expect(trackById(program, TrackId{2}) == &program.tracks[0],
         "sequence program helper should resolve tracks by stable track id");
  expect(sourceCommandById(program.tracks[0], noteCommandId) == &program.tracks[0].commands[1],
         "sequence program helper should resolve source commands by stable command id");
  expect(sourceCommandForEvent(program, note->header) == &program.tracks[0].commands[1],
         "performance event source links should resolve back to source commands");

  const auto noteEvents = performanceEventsForCommand(renderedTrack, noteCommandId);
  expect(noteEvents.size() == 1 && noteEvents[0] == &renderedTrack.events[1],
         "performance helper should collect events emitted by one source command");
  expect(performanceTrackById(performance, TrackId{2}) == &performance.tracks[0],
         "performance helper should resolve rendered tracks by stable track id");
  expect(sourceCommandForEvent(program, PerformanceEventHeader{.sourceCommand = CommandId{99}, .track = TrackId{2}}) ==
             nullptr,
         "performance source-link helper should return null for a missing command");
}

void sequenceVmReplaysInfiniteLoopsWhenRequested() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x00, 0x00};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{3}, probeRange(3, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(SequenceVmOptions{
                                                         .loopPolicy = LoopPolicy::PlayOnce,
                                                         .sequenceLoops = 2,
                                                     })
                                              .render(program, dialect);
  expect(performance.diagnostics.empty(), "configured loop-count fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 36, "two requested loop repeats should produce three playthroughs");
  for (u64 tick : {0ULL, 12ULL, 24ULL}) {
    expect(countProbeNotesAt(performance.tracks[0], tick) == 1,
           "configured loop-count fixture should emit a note at tick " + std::to_string(tick));
  }
}

void sequenceVmStopsDeclaredLoopBeforeTargetReplay() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> loopBytes{0xfb, 0x00, 0x00};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeDeclaredLoopCommand>(builder, dialect, Address{5}, probeRange(5, loopBytes.size()), loopBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence playOnce = SequenceVm().render(program, dialect);
  expect(playOnce.diagnostics.empty(), "declared-loop fixture should not report diagnostics");
  expect(playOnce.tracks[0].endTick == 12, "declared-loop should stop at the loop command by default");
  expect(playOnce.tracks[0].events.size() == 2,
         "declared-loop should not replay target setup events when no loops are requested");

  const PerformanceSequence oneLoop = SequenceVm(SequenceVmOptions{
                                                     .loopPolicy = LoopPolicy::PlayOnce,
                                                     .sequenceLoops = 1,
                                                 })
                                          .render(program, dialect);
  expect(oneLoop.tracks[0].endTick == 24, "declared-loop should honor one requested loop repeat");
  expect(countProbeNotesAt(oneLoop.tracks[0], 0) == 1 && countProbeNotesAt(oneLoop.tracks[0], 12) == 1,
         "declared-loop should replay the target only while loop budget remains");
}

void sequenceVmPreservesDeclaredLoopAsPerformanceMarkers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> loopBytes{0xfb, 0x00, 0x00};
  const CommandId noteCommand =
      addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes).id;
  const CommandId loopCommand = addProbeCommand<ProbeDeclaredLoopCommand>(builder, dialect, Address{3},
                                                                          probeRange(3, loopBytes.size()), loopBytes)
                                    .id;

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program, dialect);
  expect(performance.diagnostics.empty(), "preserved declared-loop fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12, "preserved declared-loop should stop after the first pass");

  const MarkerPerformanceEvent* loopStart = probeMarkerAt(performance.tracks[0], "Loop Start", 0);
  const MarkerPerformanceEvent* loopEnd = probeMarkerAt(performance.tracks[0], "Loop End", 12);
  expect(loopStart != nullptr && loopStart->header.sourceCommand == noteCommand,
         "preserved declared-loop should mark the declared loop target as loop start");
  expect(loopEnd != nullptr && loopEnd->header.sourceCommand == loopCommand,
         "preserved declared-loop should mark the explicit loop command as loop end");
}

void sequenceVmLoopCandidateRequiresVisitedDestination() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{10},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpToStartBytes{0xfc, 0x00, 0x00};
  const std::array<u8, 3> jumpToBodyBytes{0xfc, 0x00, 0x00};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeLoopCandidateCommand>(builder, dialect, Address{3}, probeRange(3, jumpToStartBytes.size()),
                                             jumpToStartBytes);
  addProbeCommand<ProbeLoopCandidateCommand>(builder, dialect, Address{10}, probeRange(10, jumpToBodyBytes.size()),
                                             jumpToBodyBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "loop-candidate fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12,
         "loop-candidate should allow an unvisited backward destination and stop after it repeats");
  expect(countProbeNotesAt(performance.tracks[0], 0) == 1,
         "loop-candidate should not replay the loop target after detecting the visited destination");
}

void sequenceVmLoopCandidateIgnoresRepeatState() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpToRepeatBytes{0xfe, 0x14, 0x00};
  const std::array<u8, 3> loopCandidateBytes{0xfc, 0x00, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x02, 0x0a, 0x00};

  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{3}, probeRange(3, jumpToRepeatBytes.size()),
                                    jumpToRepeatBytes);
  addProbeCommand<ProbeLoopCandidateCommand>(builder, dialect, Address{10}, probeRange(10, loopCandidateBytes.size()),
                                             loopCandidateBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{20}, probeRange(20, repeatBytes.size()), repeatBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "loop-candidate repeat-state fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12,
         "loop-candidate should honor a prior loop target even when a repeat counter is active");
  expect(countProbeNotesAt(performance.tracks[0], 0) == 1 && countProbeNotesAt(performance.tracks[0], 12) == 0,
         "loop-candidate should stop at the declared loop instead of replaying the target under repeat state");
}

void sequenceVmPreservesLoopCandidateAsPerformanceMarkers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfc, 0x00, 0x00};
  const CommandId noteCommand =
      addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes).id;
  const CommandId jumpCommand = addProbeCommand<ProbeLoopCandidateCommand>(builder, dialect, Address{3},
                                                                           probeRange(3, jumpBytes.size()), jumpBytes)
                                    .id;

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program, dialect);
  expect(performance.diagnostics.empty(), "preserved loop-candidate fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12, "preserved loop-candidate should stop after the first pass");

  const MarkerPerformanceEvent* loopStart = probeMarkerAt(performance.tracks[0], "Loop Start", 0);
  const MarkerPerformanceEvent* loopEnd = probeMarkerAt(performance.tracks[0], "Loop End", 12);
  expect(loopStart != nullptr && loopStart->header.sourceCommand == noteCommand,
         "preserved loop-candidate should mark the visited destination as loop start");
  expect(loopEnd != nullptr && loopEnd->header.sourceCommand == jumpCommand,
         "preserved loop-candidate should mark the jump command as loop end");
}

void sequenceVmPreservesLoopsAsPerformanceMarkers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{2},
      .sourceTrackNumber = 7,
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  const CommandId noteCommand =
      addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes).id;
  const CommandId jumpCommand =
      addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes).id;

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program, dialect);
  expect(performance.diagnostics.empty(), "preserve-loop VM fixture should not report diagnostics");
  expect(performance.tracks.size() == 1, "preserve-loop VM fixture should render one track");
  const PerformanceTrack& renderedTrack = performance.tracks[0];
  expect(renderedTrack.endTick == 12, "preserve-loop VM should stop after discovering the first runtime loop");

  const auto countNotesAt = [&](u64 tick) {
    return std::ranges::count_if(renderedTrack.events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick;
    });
  };
  expect(countNotesAt(0) == 1 && countNotesAt(12) == 0,
         "preserve-loop VM should emit one pass without replaying the loop body");

  const auto markerAt = [&](std::string_view text, u64 tick) -> const MarkerPerformanceEvent* {
    for (const auto& event : renderedTrack.events) {
      const auto* marker = std::get_if<MarkerPerformanceEvent>(&event);
      if (marker != nullptr && marker->text == text && marker->header.tick == tick) {
        return marker;
      }
    }
    return nullptr;
  };
  const MarkerPerformanceEvent* loopStart = markerAt("Loop Start", 0);
  const MarkerPerformanceEvent* loopEnd = markerAt("Loop End", 12);
  expect(loopStart != nullptr && loopStart->header.sourceCommand == noteCommand,
         "preserve-loop VM should link loop-start marker to the repeated command");
  expect(loopEnd != nullptr && loopEnd->header.sourceCommand == jumpCommand,
         "preserve-loop VM should link loop-end marker to the command that jumped back");

  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  const auto countMidiMarkers = [&](std::string_view text, u64 tick) {
    return std::ranges::count_if(midi.tracks[0].events, [text, tick](const MidiEvent& event) {
      const auto* marker = std::get_if<Marker>(&event);
      return marker != nullptr && marker->text == text && marker->tick == tick;
    });
  };
  expect(countMidiMarkers("Loop Start", 0) == 1 && countMidiMarkers("Loop End", 12) == 1,
         "performance MIDI renderer should preserve neutral loop markers");
}

void sequenceVmUsesDialectCommandLimitDefault() {
  const SequenceDialect dialect = probeSequenceDialect(SequenceProgramBehavior{
      .defaultLoopPolicy = LoopPolicy::Default,
      .commandLimit = 2,
  });
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program, dialect);
  expect(performance.diagnostics.size() == 1 &&
             performance.diagnostics[0].message ==
                 "Sequence VM command limit reached: track=0, address=$0005, tick=12, executed=2, limit=2",
         "sequence VM should report the track, address, tick, and active command limit");
  expect(performance.tracks[0].events.size() == 2,
         "dialect command limit should stop execution before the looping jump command");
  expect(performance.tracks[0].endTick == 12, "command-limit stop should preserve ticks from commands already run");
}

void sequenceVmFallsThroughBySourceAddressWhenDecodeOrderDiffers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};

  // Jump/call decoding often discovers a later source block first. Fallthrough
  // must still use source addresses, not command-vector order.
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{11}, probeRange(11, programBytes.size()),
                                       programBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{13}, probeRange(13, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{3}, probeRange(3, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{6}, probeRange(6, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{9}, probeRange(9, programBytes.size()), programBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "out-of-order source fallthrough fixture should not report diagnostics");
  expect(performance.tracks[0].events.size() == 5,
         "VM should execute source-contiguous commands across decoded-block order");

  const auto* finalProgram = std::get_if<InstrumentPerformanceEvent>(&performance.tracks[0].events[4]);
  expect(finalProgram != nullptr && finalProgram->header.tick == 36,
         "source-address fallthrough should reach the earlier-decoded program command");
}

void sequenceVmEmitsDialectInitialChannelDefaults() {
  const SequenceDialect dialect = probeSequenceDialect(SequenceProgramBehavior{
      .defaultLoopPolicy = LoopPolicy::Default,
      .initialReverbSend = 0.0,
      .initialMonoModeChannels = 0,
  });
  TrackProgram track{
      .id = TrackId{3},
      .sourceTrackNumber = 4,
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{0}, probeRange(0, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.tracks.size() == 1, "initial-default fixture should render one track");
  const auto& events = performance.tracks[0].events;
  expect(events.size() == 2, "VM should emit dialect initial channel defaults before source commands");

  const auto* reverb = std::get_if<ReverbPerformanceEvent>(&events[0]);
  expect(reverb != nullptr && reverb->send == 0.0 && !reverb->header.sourceCommand.valid(),
         "initial reverb should preserve explicit zero and should not pretend to come from a source command");
  const auto* mono = std::get_if<MonoModePerformanceEvent>(&events[1]);
  expect(mono != nullptr && mono->channels == 0 && !mono->header.sourceCommand.valid(),
         "initial mono mode should preserve explicit zero and should not pretend to come from a source command");

  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  const auto* midiPort = std::get_if<MidiPort>(&midi.tracks[0].events[0]);
  expect(midiPort != nullptr && midiPort->port == 0, "performance renderer should emit MIDI port metadata");
  const auto* midiReverb = std::get_if<Reverb>(&midi.tracks[0].events[1]);
  expect(midiReverb != nullptr && midiReverb->channel == 0 && midiReverb->value == 0,
         "performance renderer should lower initial reverb to MIDI CC91");
  const auto* midiMono = std::get_if<MonoMode>(&midi.tracks[0].events[2]);
  expect(midiMono != nullptr && midiMono->channel == 0 && midiMono->channels == 0,
         "performance renderer should lower initial mono mode to MIDI CC126");
}

void sequenceVmAllowsRepeatedCallsToSameSubroutine() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> callBytes{0xc0, 0x0a, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x05, 0x04};
  const std::array<u8, 1> returnBytes{0xfd};
  addProbeCommand<ProbeCallCommand>(builder, dialect, Address{0}, probeRange(0, callBytes.size()), callBytes);
  addProbeCommand<ProbeCallCommand>(builder, dialect, Address{3}, probeRange(3, callBytes.size()), callBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{6}, probeRange(6, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{10}, probeRange(10, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeReturnCommand>(builder, dialect, Address{13}, probeRange(13, returnBytes.size()), returnBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "sequence VM repeated-call fixture should not report diagnostics");
  expect(performance.tracks[0].events.size() == 2,
         "sequence VM should allow two call sites to reuse the same subroutine");
  expect(performance.tracks[0].endTick == 8, "sequence VM should return from both subroutine calls");

  for (u64 tick : {0ULL, 4ULL}) {
    const bool found = std::ranges::any_of(performance.tracks[0].events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick && note->durationTicks == 4;
    });
    expect(found, "sequence VM should emit the shared subroutine note at tick " + std::to_string(tick));
  }
}

void sequenceVmReplaysFiniteRepeatBlocks() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{3}, probeRange(3, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "sequence VM finite repeat fixture should not report diagnostics");
  expect(performance.tracks[0].events.size() == 3, "sequence VM should replay a finite repeat block");
  expect(performance.tracks[0].endTick == 36, "sequence VM should advance through each repeated note");

  for (u64 tick : {0ULL, 12ULL, 24ULL}) {
    const bool found = std::ranges::any_of(performance.tracks[0].events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick;
    });
    expect(found, "sequence VM should emit repeated notes at each playback tick");
  }
}

void sequenceVmRepeatReplayUsesCommandAddressesNotSourceOffsets() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{1003},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> jumpToOutsideBytes{0xfe, 0xd0, 0x07};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x02, 0xe8, 0x03};
  const std::array<u8, 3> jumpToSelfBytes{0xfe, 0xd0, 0x07};

  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{1000}, probeRange(100, jumpToOutsideBytes.size()),
                                    jumpToOutsideBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{1003}, probeRange(103, repeatBytes.size()),
                                      repeatBytes);
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{2000}, probeRange(200, jumpToSelfBytes.size()),
                                    jumpToSelfBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .behavior = SequenceProgramBehavior{.commandLimit = 8},
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(),
         "repeat replay should keep finite state distinct and still detect the unrelated jump loop");
}

void sequenceVmDetectsCycleWhenRepeatCommandsReuseOneCounter() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x01};
  const std::array<u8, 5> shortRepeatBytes{0xf0, 0x00, 0x02, 0x00, 0x00};
  const std::array<u8, 5> outerRepeatBytes{0xf0, 0x00, 0x04, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{3}, probeRange(3, shortRepeatBytes.size()),
                                      shortRepeatBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{8}, probeRange(8, outerRepeatBytes.size()),
                                      outerRepeatBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{13}, probeRange(13, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .behavior = SequenceProgramBehavior{.commandLimit = 100},
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "reused repeat counters should terminate through normal loop detection");
  expect(performance.tracks[0].events.size() == 4,
         "the VM should preserve the first playthrough before stopping the reused-counter cycle");
  expect(performance.tracks[0].endTick == 4,
         "the VM should stop when command, stack, and repeat-counter state recur");
}

void sequenceVmExecutesNestedCallInsideRepeat() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> callBytes{0xc0, 0x14, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x05, 0x04};
  const std::array<u8, 1> returnBytes{0xfd};
  addProbeCommand<ProbeCallCommand>(builder, dialect, Address{0}, probeRange(0, callBytes.size()), callBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{3}, probeRange(3, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{20}, probeRange(20, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeReturnCommand>(builder, dialect, Address{23}, probeRange(23, returnBytes.size()), returnBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "nested call-repeat fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12, "VM should return from a nested call during each repeat iteration");
  for (u64 tick : {0ULL, 4ULL, 8ULL}) {
    expect(countProbeNotesAt(performance.tracks[0], tick) == 1,
           "VM should emit the repeated subroutine note at tick " + std::to_string(tick));
  }
}

void sequenceVmExecutesRepeatInsideCall() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> callBytes{0xc0, 0x14, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x05, 0x04};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x14, 0x00};
  const std::array<u8, 1> returnBytes{0xfd};
  addProbeCommand<ProbeCallCommand>(builder, dialect, Address{0}, probeRange(0, callBytes.size()), callBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{3}, probeRange(3, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{20}, probeRange(20, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{23}, probeRange(23, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeReturnCommand>(builder, dialect, Address{28}, probeRange(28, returnBytes.size()), returnBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "repeat-inside-call fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12, "VM should finish a repeated subroutine and return to the caller");
  for (u64 tick : {0ULL, 4ULL, 8ULL}) {
    expect(countProbeNotesAt(performance.tracks[0], tick) == 1,
           "VM should emit the subroutine repeat note at tick " + std::to_string(tick));
  }
}

void sequenceVmRunsRepeatBreakSideEffectsOnlyWhenBranchTaken() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 4> repeatBreakBytes{0xf1, 0x00, 0x0c, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatBreakCommand>(builder, dialect, Address{3}, probeRange(3, repeatBreakBytes.size()),
                                           repeatBreakBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{7}, probeRange(7, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{12}, probeRange(12, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "repeat-break fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 36, "repeat-break should skip the final repeat command");
  for (u64 tick : {0ULL, 12ULL, 24ULL}) {
    expect(countProbeNotesAt(performance.tracks[0], tick) == 1,
           "repeat-break fixture should emit note at tick " + std::to_string(tick));
  }

  const auto sideEffects = std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* instrument = std::get_if<InstrumentPerformanceEvent>(&event);
    return instrument != nullptr && instrument->program == 99 && instrument->header.tick == 36;
  });
  expect(sideEffects == 1, "repeat-break command should emit side effects only when the break branch is taken");
}

void sequenceVmRepeatBreakCanBranchToPreviouslyVisitedCode() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x14, 0x00};
  const std::array<u8, 4> repeatBreakBytes{0xf1, 0x00, 0x00, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x02, 0x14, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{3}, probeRange(3, jumpBytes.size()), jumpBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{20}, probeRange(20, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatBreakCommand>(builder, dialect, Address{23}, probeRange(23, repeatBreakBytes.size()),
                                           repeatBreakBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{27}, probeRange(27, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{32}, probeRange(32, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "repeat-break branch-to-visited fixture should not report diagnostics");
  expect(countProbeNotesAt(performance.tracks[0], 36) == 1,
         "repeat-break should execute a branch target even if that command ran earlier");
  expect(performance.tracks[0].endTick == 48,
         "repeat-break branch-to-visited fixture should stop only after the branch target note plays");
}

void sequenceVmPreservesLoopMarkersForInteriorJumpTarget() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x05, 0x00};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  const CommandId loopStartCommand =
      addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{5}, probeRange(5, noteBytes.size()), noteBytes).id;
  const CommandId jumpCommand =
      addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{8}, probeRange(8, jumpBytes.size()), jumpBytes).id;

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program, dialect);
  expect(performance.diagnostics.empty(), "interior loop target fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 24, "interior loop target should stop at the repeated command");
  const MarkerPerformanceEvent* loopStart = probeMarkerAt(performance.tracks[0], "Loop Start", 12);
  const MarkerPerformanceEvent* loopEnd = probeMarkerAt(performance.tracks[0], "Loop End", 24);
  expect(loopStart != nullptr && loopStart->header.sourceCommand == loopStartCommand,
         "preserve-loop marker should attach to the interior repeated command");
  expect(loopEnd != nullptr && loopEnd->header.sourceCommand == jumpCommand,
         "preserve-loop marker should attach to the jump into the decoded block");
}

void sequenceVmStopsAllTracksAtEarliestLoopTick() {
  const SequenceDialect dialect = probeSequenceDialect();
  const std::array<u8, 3> note12Bytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> note20Bytes{0x90, 0x00, 0x14};
  const std::array<u8, 3> jumpTrack0Bytes{0xfe, 0x00, 0x00};
  const std::array<u8, 3> jumpTrack1Bytes{0xfe, 0x64, 0x00};

  TrackProgram track0{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder0{track0};
  addProbeCommand<ProbeNoteCommand>(builder0, dialect, Address{0}, probeRange(0, note12Bytes.size()), note12Bytes);
  addProbeCommand<ProbeJumpCommand>(builder0, dialect, Address{3}, probeRange(3, jumpTrack0Bytes.size()),
                                    jumpTrack0Bytes);

  TrackProgram track1{
      .id = TrackId{1},
      .startAddress = Address{100},
  };
  TrackProgramBuilder builder1{track1};
  addProbeCommand<ProbeNoteCommand>(builder1, dialect, Address{100}, probeRange(100, note20Bytes.size()), note20Bytes);
  addProbeCommand<ProbeNoteCommand>(builder1, dialect, Address{103}, probeRange(103, note20Bytes.size()), note20Bytes);
  addProbeCommand<ProbeJumpCommand>(builder1, dialect, Address{106}, probeRange(106, jumpTrack1Bytes.size()),
                                    jumpTrack1Bytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .behavior = SequenceProgramBehavior{.stopAllTracksAtFirstLoop = true},
      .tracks = {track0, track1},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "synchronized first-loop fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12, "looping track should stop at its first repeated command");
  expect(performance.tracks[1].endTick == 20,
         "other tracks should stop after the command crossing the first loop tick");
  expect(countProbeNotesAt(performance.tracks[1], 0) == 1 && countProbeNotesAt(performance.tracks[1], 20) == 0,
         "stopAllTracksAtFirstLoop should prevent later-track events past the earliest loop tick");
}

void sequenceVmSynchronizedDryRunDoesNotDuplicateDiagnostics() {
  const SequenceDialect dialect = probeSequenceDialect();
  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpLoopBytes{0xfe, 0x00, 0x00};
  const std::array<u8, 3> jumpMissingBytes{0xfe, 0x63, 0x00};

  TrackProgram track0{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder0{track0};
  addProbeCommand<ProbeNoteCommand>(builder0, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(builder0, dialect, Address{3}, probeRange(3, jumpLoopBytes.size()), jumpLoopBytes);

  TrackProgram track1{
      .id = TrackId{1},
      .startAddress = Address{100},
  };
  TrackProgramBuilder builder1{track1};
  const SourceRange missingJumpRange = probeRange(100, jumpMissingBytes.size());
  addProbeCommand<ProbeJumpCommand>(builder1, dialect, Address{100}, missingJumpRange, jumpMissingBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .behavior = SequenceProgramBehavior{.stopAllTracksAtFirstLoop = true},
      .tracks = {track0, track1},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.size() == 1,
         "synchronized dry-run diagnostics should not be copied into the final render");
  expectDiagnosticRange(performance.diagnostics, "Sequence jump target $0063 was not decoded", missingJumpRange);
}

void sequenceVmDoesNotWrapCommandAddressOverflow() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{std::numeric_limits<u64>::max() - 1},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x04};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{std::numeric_limits<u64>::max() - 1}, SourceRange{},
                                    noteBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{1}, SourceRange{}, noteBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "address-overflow fixture should not report diagnostics");
  expect(performance.tracks[0].events.size() == 1 && performance.tracks[0].endTick == 4,
         "source-address fallthrough should not wrap around the address space");
}

void sequenceVmReportsMissingJumpTargetAfterEmittedEvents() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x63, 0x00};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  const SourceRange jumpRange = probeRange(3, jumpBytes.size());
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{3}, jumpRange, jumpBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.tracks[0].events.size() == 1 && performance.tracks[0].endTick == 12,
         "VM should preserve events emitted before a missing jump target");
  expectDiagnosticRange(performance.diagnostics, "Sequence jump target $0063 was not decoded", jumpRange);
}

struct ScheduledProbeProgramState {
  u32 sharedValue = 0;
};

std::any createScheduledProbeProgramState(const SequenceProgram&) {
  return ScheduledProbeProgramState{};
}

Effects executeScheduledProbe(const SourceCommand& command, std::any& programState, std::any&, PerformanceEmitter& out,
                              VmApi& vm) {
  auto& state = std::any_cast<ScheduledProbeProgramState&>(programState);
  if (command.address.value == 0 || command.address.value == 2) {
    state.sharedValue = command.opcode;
    return Effects::none();
  }
  if (command.address.value == 1) {
    return Effects::wait(command.opcode);
  }
  if (command.address.value == 10 || command.address.value == 11) {
    out.note(state.sharedValue, 1.0, 1);
    return Effects::wait(command.opcode);
  }
  return Effects{.step = vm.end()};
}

void sequenceVmSchedulesSemanticTracksAgainstOneProgramState() {
  const SequenceDialect dialect{
      .id = DialectId{.value = "scheduled-probe"},
      .timebase = Timebase{.ppqn = 48},
      .createProgramState = createScheduledProbeProgramState,
      .executeSemantic = executeScheduledProbe,
  };

  TrackProgram track0{.id = TrackId{0}, .startAddress = Address{0}};
  TrackProgramBuilder builder0(track0);
  builder0.addSemantic(Address{0}, 7, 1, {}, {}, DecodeFlow::fallthroughTo(Address{1}));
  builder0.addSemantic(Address{1}, 4, 1, {}, {}, DecodeFlow::fallthroughTo(Address{2}));
  builder0.addSemantic(Address{2}, 9, 1, {}, {}, DecodeFlow::fallthroughTo(Address{3}));
  builder0.addSemantic(Address{3}, 0, 1, {}, {}, DecodeFlow::terminalFlow());

  TrackProgram track1{.id = TrackId{1}, .sourceTrackNumber = 1, .startAddress = Address{10}};
  TrackProgramBuilder builder1(track1);
  builder1.addSemantic(Address{10}, 2, 1, {}, {}, DecodeFlow::fallthroughTo(Address{11}));
  builder1.addSemantic(Address{11}, 0, 1, {}, {}, DecodeFlow::fallthroughTo(Address{12}));
  builder1.addSemantic(Address{12}, 0, 1, {}, {}, DecodeFlow::terminalFlow());

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track0, track1},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "semantic scheduler fixture should render without diagnostics");
  expect(performance.tracks.size() == 2 && performance.tracks[1].events.size() == 2,
         "semantic scheduler should preserve both tracks and their events");

  const auto& first = std::get<NotePerformanceEvent>(performance.tracks[1].events[0]);
  const auto& second = std::get<NotePerformanceEvent>(performance.tracks[1].events[1]);
  expect(first.header.tick == 0 && first.key == 7.0,
         "stable same-tick ordering should expose track zero's shared update to track one");
  expect(second.header.tick == 2 && second.key == 7.0,
         "the scheduler should not run a later-tick update before an earlier track event");
}

Effects executeScheduledLoopProbe(const SourceCommand& command, std::any&, std::any&, PerformanceEmitter& out,
                                  VmApi& vm) {
  switch (command.address.value) {
    case 0:
    case 100:
    case 110:
      out.note(60.0, 1.0, 6);
      return Effects::wait(4);
    case 1:
      return Effects{.step = vm.loopCandidate(Address{0})};
    case 101:
      return Effects{.step = vm.loopCandidate(Address{110})};
    case 111:
      return Effects{.step = vm.loopCandidate(Address{110})};
    default:
      return Effects{.step = vm.end()};
  }
}

void sequenceVmCoordinatesSemanticLoopsAtSequenceScope() {
  const SequenceDialect dialect{
      .id = DialectId{.value = "scheduled-loop-probe"},
      .timebase = Timebase{.ppqn = 48},
      .executeSemantic = executeScheduledLoopProbe,
  };

  TrackProgram track0{.id = TrackId{0}, .startAddress = Address{0}};
  TrackProgramBuilder builder0(track0);
  builder0.addSemantic(Address{0}, 0, 1, {}, {}, DecodeFlow::fallthroughTo(Address{1}));
  builder0.addSemantic(Address{1}, 0, 1, {}, {}, DecodeFlow::jump(Address{0}));

  // This track first jumps into an unvisited block. A per-track loop detector
  // stops track 0 too early while this track is still establishing its loop.
  TrackProgram track1{.id = TrackId{1}, .startAddress = Address{100}};
  TrackProgramBuilder builder1(track1);
  builder1.addSemantic(Address{100}, 0, 1, {}, {}, DecodeFlow::fallthroughTo(Address{101}));
  builder1.addSemantic(Address{101}, 0, 1, {}, {}, DecodeFlow::jump(Address{110}));
  builder1.addSemantic(Address{110}, 0, 1, {}, {}, DecodeFlow::fallthroughTo(Address{111}));
  builder1.addSemantic(Address{111}, 0, 1, {}, {}, DecodeFlow::jump(Address{110}));

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track0, track1},
  };

  const PerformanceSequence playOnce = SequenceVm().render(program, dialect);
  expect(playOnce.diagnostics.empty(), "sequence-wide semantic loop fixture should not report diagnostics");
  expect(playOnce.tracks[0].endTick == 8 && playOnce.tracks[1].endTick == 8,
         "semantic tracks should stop together at the longest loop-budget endpoint");
  for (const auto& track : playOnce.tracks) {
    expect(countProbeNotesAt(track, 0) == 1 && countProbeNotesAt(track, 4) == 1,
           "shorter semantic loops should keep playing while longer loops reach their endpoint");
    expect(countProbeNotesAt(track, 8) == 0,
           "semantic loop cutoff should remove events emitted at the repeated boundary");
    const auto crossingNote = std::ranges::find_if(track.events, [](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == 4;
    });
    expect(crossingNote != track.events.end() && std::get<NotePerformanceEvent>(*crossingNote).durationTicks == 4,
           "semantic loop cutoff should clip notes that cross the common boundary");
  }

  const PerformanceSequence oneRepeat = SequenceVm(SequenceVmOptions{
                                                       .loopPolicy = LoopPolicy::PlayOnce,
                                                       .sequenceLoops = 1,
                                                   })
                                            .render(program, dialect);
  expect(oneRepeat.tracks[0].endTick == 12 && oneRepeat.tracks[1].endTick == 12,
         "one requested semantic loop should extend the longest sequence boundary once");
  expect(countProbeNotesAt(oneRepeat.tracks[0], 8) == 1 && countProbeNotesAt(oneRepeat.tracks[1], 8) == 1,
         "one requested semantic loop should replay every track together");
}

}  // namespace

void runValueSequenceVmTests() {
  sequenceVmExecutesSourceCommandsAndStopsAtPlayOnceLoop();
  sequenceVmReplaysInfiniteLoopsWhenRequested();
  sequenceVmStopsDeclaredLoopBeforeTargetReplay();
  sequenceVmPreservesDeclaredLoopAsPerformanceMarkers();
  sequenceVmLoopCandidateRequiresVisitedDestination();
  sequenceVmLoopCandidateIgnoresRepeatState();
  sequenceVmPreservesLoopCandidateAsPerformanceMarkers();
  sequenceVmPreservesLoopsAsPerformanceMarkers();
  sequenceVmUsesDialectCommandLimitDefault();
  sequenceVmFallsThroughBySourceAddressWhenDecodeOrderDiffers();
  sequenceVmEmitsDialectInitialChannelDefaults();
  sequenceVmAllowsRepeatedCallsToSameSubroutine();
  sequenceVmReplaysFiniteRepeatBlocks();
  sequenceVmRepeatReplayUsesCommandAddressesNotSourceOffsets();
  sequenceVmDetectsCycleWhenRepeatCommandsReuseOneCounter();
  sequenceVmExecutesNestedCallInsideRepeat();
  sequenceVmExecutesRepeatInsideCall();
  sequenceVmRunsRepeatBreakSideEffectsOnlyWhenBranchTaken();
  sequenceVmRepeatBreakCanBranchToPreviouslyVisitedCode();
  sequenceVmPreservesLoopMarkersForInteriorJumpTarget();
  sequenceVmStopsAllTracksAtEarliestLoopTick();
  sequenceVmSynchronizedDryRunDoesNotDuplicateDiagnostics();
  sequenceVmDoesNotWrapCommandAddressOverflow();
  sequenceVmReportsMissingJumpTargetAfterEmittedEvents();
  sequenceVmSchedulesSemanticTracksAgainstOneProgramState();
  sequenceVmCoordinatesSemanticLoopsAtSequenceScope();
}

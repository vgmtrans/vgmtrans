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
      .sourceTrackNumber = 7,
      .startAddress = Address{0},
  };

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeProgramCommand>(track, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  track.commands.back().annotation = SourceAnnotationId{10};
  const CommandId noteCommandId =
      addProbeCommand<ProbeNoteCommand>(track, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  track.commands.back().annotation = SourceAnnotationId{11};
  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes);
  track.commands.back().annotation = SourceAnnotationId{12};
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);
  track.commands.back().annotation = SourceAnnotationId{13};

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(),
         "sequence VM should render the probe sequence without diagnostics" +
             (performance.diagnostics.empty() ? std::string{} : ": " + performance.diagnostics.front().message));
  expect(performance.tracks.size() == 1, "sequence VM should render one performance track");
  const PerformanceTrack& renderedTrack = performance.tracks[0];
  expect(renderedTrack.id == TrackId{0} && renderedTrack.sourceTrackNumber == 7,
         "performance track should use positional identity while preserving its source track number");
  expect(renderedTrack.endTick == 12,
         "default play-once loop policy should stop at the first repeated command; end tick was " +
             std::to_string(renderedTrack.endTick));
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
  expect(program.tracks[0].command(noteCommandId) == &program.tracks[0].commands[1],
         "track program should resolve source commands by positional command id");
  expect(sourceCommandForEvent(program, note->header) == &program.tracks[0].commands[1],
         "performance event source links should resolve back to source commands");

  const auto noteEvents = performanceEventsForCommand(renderedTrack, noteCommandId);
  expect(noteEvents.size() == 1 && noteEvents[0] == &renderedTrack.events[1],
         "performance helper should collect events emitted by one source command");
  expect(performanceTrackById(performance, TrackId{0}) == &performance.tracks[0],
         "performance helper should resolve rendered tracks by stable track id");
  expect(sourceCommandForEvent(program, PerformanceEventHeader{.sourceCommand = CommandId{99}, .track = TrackId{0}}) ==
             nullptr,
         "performance source-link helper should return null for a missing command");

  expect(performance.sourceSpans ==
             std::vector<SourcePlaybackSpan>{
                 {.annotation = SourceAnnotationId{10}, .beginTick = 0, .endTick = 1},
                 {.annotation = SourceAnnotationId{11}, .beginTick = 0, .endTick = 12},
             },
         "source timeline should preserve point and note durations while trimming the final loop boundary");
}

void sequenceVmTimesCommandsThatEmitNoPerformanceEvents() {
  const SequenceDialect dialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
  const SequenceRuntime runtime{
              .execute = [](const SourceCommand& command, std::any&, std::any&, PerformanceEmitter&,
                            VmApi&) { return command.address.value == 0 ? Effects::wait(7) : Effects{}; },
  };
  TrackProgram track{.startAddress = Address{0}};
  track.addCommand(Address{0}, 0, {}, {}, CommandFlow::fallthroughTo(Address{1}), SourceAnnotationId{20});
  track.addCommand(Address{1}, 0, {},
                      {SemanticOperand{
                          .value = u64{6},
                          .name = "channel",
                          .role = SemanticOperandRole::Channel,
                      }},
                      CommandFlow::end(Address{2}), SourceAnnotationId{21});
  const SequenceProgram program{
      .runtime = runtime,
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.tracks[0].events.empty(),
         "eventless source-timeline fixture should not invent musical performance events");
  expect(performance.sourceSpans ==
             std::vector<SourcePlaybackSpan>{
                 {.annotation = SourceAnnotationId{20}, .beginTick = 0, .endTick = 7},
                 {.annotation = SourceAnnotationId{21}, .channel = 6, .beginTick = 7, .endTick = 8},
             },
         "source timeline should preserve eventless command timing and semantic channels");
}

void sequenceVmPreservesPitchMotionThroughNoteRelease() {
  const SequenceDialect dialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
  const SequenceRuntime runtime{
              .execute =
                  [](const SourceCommand& command, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
                    if (command.address.value != 0) {
                      return Effects{};
                    }
                    const PerformanceNoteId note = out.note(60, 1.0, 2);
                    out.pitchSlide(note, 60, 64, 4);
                    return Effects::wait(8);
                  },
  };
  TrackProgram track{.startAddress = Address{0}};
  track.addCommand(Address{0}, 0, {}, {}, CommandFlow::fallthroughTo(Address{1}));
  track.addCommand(Address{1}, 0, {}, {}, CommandFlow::end(Address{2}));

  const PerformanceSequence performance = SequenceVm().render(SequenceProgram{
      .runtime = runtime,
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  });
  expect(performance.tracks[0].automations.size() == 1 && performance.tracks[0].automations[0].realization.endTick == 4,
         "sequence finalization should preserve pitch motion beyond note-off for a sounding release tail");
}

void sequenceVmReplaysInfiniteLoopsWhenRequested() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x00, 0x00};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{3}, probeRange(3, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(SequenceVmOptions{
                                                         .loopPolicy = LoopPolicy::PlayOnce,
                                                         .sequenceLoops = 2,
                                                     })
                                              .render(program);
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
      .startAddress = Address{0},
  };

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> loopBytes{0xfb, 0x00, 0x00};
  addProbeCommand<ProbeProgramCommand>(track, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeDeclaredLoopCommand>(track, dialect, Address{5}, probeRange(5, loopBytes.size()), loopBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence playOnce = SequenceVm().render(program);
  expect(playOnce.diagnostics.empty(), "declared-loop fixture should not report diagnostics");
  expect(playOnce.tracks[0].endTick == 12, "declared-loop should stop at the loop command by default");
  expect(playOnce.tracks[0].events.size() == 2,
         "declared-loop should not replay target setup events when no loops are requested");

  const PerformanceSequence oneLoop = SequenceVm(SequenceVmOptions{
                                                     .loopPolicy = LoopPolicy::PlayOnce,
                                                     .sequenceLoops = 1,
                                                 })
                                          .render(program);
  expect(oneLoop.tracks[0].endTick == 24, "declared-loop should honor one requested loop repeat");
  expect(countProbeNotesAt(oneLoop.tracks[0], 0) == 1 && countProbeNotesAt(oneLoop.tracks[0], 12) == 1,
         "declared-loop should replay the target only while loop budget remains");
}

void sequenceVmPreservesDeclaredLoopAsPerformanceMarkers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> loopBytes{0xfb, 0x00, 0x00};
  const CommandId noteCommand =
      addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  const CommandId loopCommand = addProbeCommand<ProbeDeclaredLoopCommand>(track, dialect, Address{3},
                                                                          probeRange(3, loopBytes.size()), loopBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program);
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
      .startAddress = Address{10},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpToStartBytes{0xfc, 0x00, 0x00};
  const std::array<u8, 3> jumpToBodyBytes{0xfc, 0x00, 0x00};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeLoopCandidateCommand>(track, dialect, Address{3}, probeRange(3, jumpToStartBytes.size()),
                                             jumpToStartBytes);
  addProbeCommand<ProbeLoopCandidateCommand>(track, dialect, Address{10}, probeRange(10, jumpToBodyBytes.size()),
                                             jumpToBodyBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "loop-candidate fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12,
         "loop-candidate should allow an unvisited backward destination and stop after it repeats");
  expect(countProbeNotesAt(performance.tracks[0], 0) == 1,
         "loop-candidate should not replay the loop target after detecting the visited destination");

  SequenceProgram explicitLoopsOnly = program;
  explicitLoopsOnly.behavior.inferLoopsFromRepeatedState = false;
  const PerformanceSequence explicitPerformance = SequenceVm().render(explicitLoopsOnly);
  expect(explicitPerformance.tracks[0].endTick == 12,
         "disabling repeated-state inference should preserve explicit loop-candidate detection");
}

void sequenceVmLoopCandidateIgnoresRepeatState() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpToRepeatBytes{0xfe, 0x14, 0x00};
  const std::array<u8, 3> loopCandidateBytes{0xfc, 0x00, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x02, 0x0a, 0x00};

  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{3}, probeRange(3, jumpToRepeatBytes.size()),
                                    jumpToRepeatBytes);
  addProbeCommand<ProbeLoopCandidateCommand>(track, dialect, Address{10}, probeRange(10, loopCandidateBytes.size()),
                                             loopCandidateBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{20}, probeRange(20, repeatBytes.size()), repeatBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "loop-candidate repeat-state fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 12,
         "loop-candidate should honor a prior loop target even when a repeat counter is active");
  expect(countProbeNotesAt(performance.tracks[0], 0) == 1 && countProbeNotesAt(performance.tracks[0], 12) == 0,
         "loop-candidate should stop at the declared loop instead of replaying the target under repeat state");
}

void sequenceVmPreservesLoopCandidateAsPerformanceMarkers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfc, 0x00, 0x00};
  const CommandId noteCommand =
      addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  const CommandId jumpCommand = addProbeCommand<ProbeLoopCandidateCommand>(track, dialect, Address{3},
                                                                           probeRange(3, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program);
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
      .sourceTrackNumber = 7,
      .startAddress = Address{0},
  };

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  addProbeCommand<ProbeProgramCommand>(track, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  const CommandId noteCommand =
      addProbeCommand<ProbeNoteCommand>(track, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  const CommandId jumpCommand =
      addProbeCommand<ProbeJumpCommand>(track, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program);
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

  const MidiSequence midi = renderMidiSequence(performance);
  const auto countMidiMarkers = [&](std::string_view text, u64 tick) {
    return std::ranges::count_if(midi.tracks[0].events, [text, tick](const MidiEvent& event) {
      const auto* marker = std::get_if<Marker>(&event);
      return marker != nullptr && marker->text == text && marker->tick == tick;
    });
  };
  expect(countMidiMarkers("Loop Start", 0) == 1 && countMidiMarkers("Loop End", 12) == 1,
         "performance MIDI renderer should preserve neutral loop markers");
}

void sequenceVmUsesProgramCommandLimit() {
  const SequenceDialect dialect = probeSequenceDialect(SequenceProgramBehavior{
      .commandLimit = 2,
  });
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  addProbeCommand<ProbeProgramCommand>(track, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program);
  expect(performance.diagnostics.size() == 1 &&
             performance.diagnostics[0].message ==
                 "Sequence VM command limit reached: track=0, address=$0005, tick=12, executed=2, limit=2",
         "sequence VM should report the track, address, tick, and active command limit");
  expect(performance.tracks[0].events.size() == 2,
         "dialect command limit should stop execution before the looping jump command");
  expect(performance.tracks[0].endTick == 12, "command-limit stop should preserve ticks from commands already run");
}

void sequenceVmUsesInitialTempoAndGlobalEventOrder() {
  const SequenceDialect tempoDialect = probeSequenceDialect(SequenceProgramBehavior{
      .initialTempoMicrosecondsPerQuarter = 750'000,
  });
  const SequenceProgram defaultTempoProgram{
      .runtime = probeSequenceRuntime(),
      .timebase = tempoDialect.timebase,
      .behavior = tempoDialect.behavior,
  };
  expect(SequenceVm().render(defaultTempoProgram).initialTempoMicrosecondsPerQuarter == 750'000,
         "sequence VM should retain a dialect's source initial tempo");

  SequenceProgram overriddenTempoProgram = defaultTempoProgram;
  overriddenTempoProgram.behavior.initialTempoMicrosecondsPerQuarter = 600'000;
  expect(SequenceVm().render(overriddenTempoProgram).initialTempoMicrosecondsPerQuarter == 600'000,
         "a parsed program should be able to override its dialect's initial tempo");

  const SequenceDialect orderDialect = probeSequenceDialect();
  const auto makeTrack = [&](u32 trackNumber, u32 address, u8 program) {
    TrackProgram track{
        .sourceTrackNumber = trackNumber,
        .startAddress = Address{address},
    };
    const std::array<u8, 2> bytes{0x80, program};
    addProbeCommand<ProbeProgramCommand>(track, orderDialect, Address{address}, probeRange(address, bytes.size()),
                                         bytes);
    return track;
  };
  const SequenceProgram orderedProgram{
      .runtime = probeSequenceRuntime(),
      .timebase = orderDialect.timebase,
      .behavior = orderDialect.behavior,
      .tracks = {makeTrack(0, 0, 1), makeTrack(1, 16, 2)},
  };
  const PerformanceSequence ordered = SequenceVm().render(orderedProgram);
  const auto& first = std::get<InstrumentPerformanceEvent>(ordered.tracks[0].events.front());
  const auto& second = std::get<InstrumentPerformanceEvent>(ordered.tracks[1].events.front());
  expect(first.header.sequence < second.header.sequence,
         "sequence VM event order should be song-wide so same-tick global state changes order across channels");
}

void sequenceVmFallsThroughBySourceAddressWhenDecodeOrderDiffers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};

  // Jump/call decoding often discovers a later source block first. Fallthrough
  // must still use source addresses, not command-vector order.
  addProbeCommand<ProbeProgramCommand>(track, dialect, Address{11}, probeRange(11, programBytes.size()),
                                       programBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{13}, probeRange(13, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{3}, probeRange(3, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{6}, probeRange(6, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeProgramCommand>(track, dialect, Address{9}, probeRange(9, programBytes.size()), programBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "out-of-order source fallthrough fixture should not report diagnostics");
  expect(performance.tracks[0].events.size() == 5,
         "VM should execute source-contiguous commands across decoded-block order");

  const auto* finalProgram = std::get_if<InstrumentPerformanceEvent>(&performance.tracks[0].events[4]);
  expect(finalProgram != nullptr && finalProgram->header.tick == 36,
         "source-address fallthrough should reach the earlier-decoded program command");
}

void sequenceVmEmitsProgramInitialChannelState() {
  const SequenceDialect dialect = probeSequenceDialect(
      SequenceProgramBehavior{
          .initialSourceInstrument = InstrumentIdentity{.domain = "probe", .key = 7},
          .initialLevel = 0.0,
          .initialExpression = 0.5,
          .initialReverbSend = 0.0,
          .initialMonoModeChannels = 0,
      },
      StereoBalance{0.25, 0.75});
  TrackProgram track{
      .sourceTrackNumber = 4,
      .startAddress = Address{0},
  };

  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{0}, probeRange(0, endBytes.size()), endBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.tracks.size() == 1, "initial-default fixture should render one track");
  const auto& events = performance.tracks[0].events;
  expect(events.size() == 6, "VM should emit program initial channel state before source commands");

  const auto* reverb = std::get_if<ReverbPerformanceEvent>(&events[0]);
  expect(reverb != nullptr && reverb->send == 0.0 && !reverb->header.sourceCommand.valid(),
         "initial reverb should preserve explicit zero and should not pretend to come from a source command");
  const auto* level = std::get_if<LevelPerformanceEvent>(&events[1]);
  expect(level != nullptr && level->linearGain == 0.0 && !level->header.sourceCommand.valid(),
         "initial level should preserve explicit zero and should not pretend to come from a source command");
  const auto* expression = std::get_if<ExpressionPerformanceEvent>(&events[2]);
  expect(expression != nullptr && expression->linearGain == 0.5 && !expression->header.sourceCommand.valid(),
         "initial expression should preserve its physical gain and should not pretend to come from a source command");
  const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&events[3]);
  expect(balance != nullptr && balance->leftGain == 0.25 && balance->rightGain == 0.75 &&
             !balance->header.sourceCommand.valid(),
         "initial stereo balance should preserve physical channel gains without inventing a source command");
  const auto* mono = std::get_if<MonoModePerformanceEvent>(&events[4]);
  expect(mono != nullptr && mono->channels == 0 && !mono->header.sourceCommand.valid(),
         "initial mono mode should preserve explicit zero and should not pretend to come from a source command");
  const auto* instrument = std::get_if<InstrumentPerformanceEvent>(&events[5]);
  expect(instrument != nullptr && instrument->sourceInstrument == InstrumentIdentity{.domain = "probe", .key = 7} &&
             !instrument->header.sourceCommand.valid(),
         "initial source instrument should preserve its identity without inventing a source command");

  const MidiSequence midi = renderMidiSequence(performance);
  const auto* midiPort = std::get_if<MidiPort>(&midi.tracks[0].events[0]);
  expect(midiPort != nullptr && midiPort->port == 0, "performance renderer should emit MIDI port metadata");
  const auto* midiReverb = std::get_if<Reverb>(&midi.tracks[0].events[1]);
  expect(midiReverb != nullptr && midiReverb->channel == 0 && midiReverb->value == 0,
         "performance renderer should lower initial reverb to MIDI CC91");
  expect(std::holds_alternative<Volume>(midi.tracks[0].events[2]) &&
             std::holds_alternative<Expression>(midi.tracks[0].events[3]),
         "performance renderer should lower initial level and expression to their distinct MIDI controllers");
  const auto* midiMono = std::get_if<MonoMode>(&midi.tracks[0].events[6]);
  expect(midiMono != nullptr && midiMono->channel == 0 && midiMono->channels == 0,
         "performance renderer should lower initial mono mode to MIDI CC126");
}

void sequenceVmEmitsInitialMasterLevelOnce() {
  const SequenceDialect dialect = probeSequenceDialect(SequenceProgramBehavior{.initialMasterLevel = 0.25});
  const auto makeTrack = [&](u32 id) {
    TrackProgram track{.sourceTrackNumber = id, .startAddress = Address{0}};
    const std::array<u8, 1> endBytes{0xff};
    addProbeCommand<ProbeEndCommand>(track, dialect, Address{0}, probeRange(id, endBytes.size()), endBytes);
    return track;
  };
  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {makeTrack(3), makeTrack(4)},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  const auto first = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<MasterLevelPerformanceEvent>(event);
  });
  const auto second = std::ranges::find_if(performance.tracks[1].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<MasterLevelPerformanceEvent>(event);
  });
  expect(first != performance.tracks[0].events.end() &&
             std::get<MasterLevelPerformanceEvent>(*first).linearGain == 0.25 &&
             !std::get<MasterLevelPerformanceEvent>(*first).header.sourceCommand.valid() &&
             second == performance.tracks[1].events.end(),
         "VM should initialize song-wide master gain once, independently of per-track levels");
}

void sequenceVmExposesSubroutineStateFromItsCallStack() {
  const SequenceDialect dialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
  const SequenceRuntime runtime{
              .execute =
                  [](const SourceCommand& command, std::any&, std::any&, PerformanceEmitter& out, VmApi& vm) {
                    if (command.address.value != 0) {
                      out.instrument(0, vm.inSubroutine() ? 1 : 0);
                    }
                    return Effects{};
                  },
  };
  TrackProgram track{.startAddress = Address{0}};
  track.addCommand(Address{0}, 0, {}, {}, CommandFlow::call(Address{10}, Address{1}));
  track.addCommand(Address{1}, 0, {}, {}, CommandFlow::end(Address{2}));
  track.addCommand(Address{10}, 0, {}, {}, CommandFlow::return_(Address{11}));

  const SequenceProgram program{
      .runtime = runtime,
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.tracks[0].events.size() == 2,
         "subroutine-state fixture should visit the called command and its continuation");
  const auto& called = std::get<InstrumentPerformanceEvent>(performance.tracks[0].events[0]);
  const auto& continued = std::get<InstrumentPerformanceEvent>(performance.tracks[0].events[1]);
  expect(called.program == 1 && continued.program == 0,
         "VM should expose call-stack state inside a subroutine and clear it after return");
}

void sequenceVmAllowsRepeatedCallsToSameSubroutine() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> callBytes{0xc0, 0x0a, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x05, 0x04};
  const std::array<u8, 1> returnBytes{0xfd};
  addProbeCommand<ProbeCallCommand>(track, dialect, Address{0}, probeRange(0, callBytes.size()), callBytes);
  addProbeCommand<ProbeCallCommand>(track, dialect, Address{3}, probeRange(3, callBytes.size()), callBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{6}, probeRange(6, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{10}, probeRange(10, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeReturnCommand>(track, dialect, Address{13}, probeRange(13, returnBytes.size()), returnBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
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
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{3}, probeRange(3, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
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
      .startAddress = Address{1003},
  };

  const std::array<u8, 3> jumpToOutsideBytes{0xfe, 0xd0, 0x07};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x02, 0xe8, 0x03};
  const std::array<u8, 3> jumpToSelfBytes{0xfe, 0xd0, 0x07};

  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{1000}, probeRange(100, jumpToOutsideBytes.size()),
                                    jumpToOutsideBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{1003}, probeRange(103, repeatBytes.size()),
                                      repeatBytes);
  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{2000}, probeRange(200, jumpToSelfBytes.size()),
                                    jumpToSelfBytes);

  SequenceProgram program = dialect.makeProgram();
  program.runtime = probeSequenceRuntime();
  program.behavior.commandLimit = 8;
  program.tracks = {track};

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(),
         "repeat replay should keep finite state distinct and still detect the unrelated jump loop");
}

void sequenceVmDetectsCycleWhenRepeatCommandsReuseOneCounter() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x01};
  const std::array<u8, 5> shortRepeatBytes{0xf0, 0x00, 0x02, 0x00, 0x00};
  const std::array<u8, 5> outerRepeatBytes{0xf0, 0x00, 0x04, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{3}, probeRange(3, shortRepeatBytes.size()),
                                      shortRepeatBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{8}, probeRange(8, outerRepeatBytes.size()),
                                      outerRepeatBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{13}, probeRange(13, endBytes.size()), endBytes);

  SequenceProgram program = dialect.makeProgram();
  program.runtime = probeSequenceRuntime();
  program.behavior.commandLimit = 100;
  program.tracks = {track};

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "reused repeat counters should terminate through normal loop detection");
  expect(performance.tracks[0].events.size() == 4,
         "the VM should preserve the first playthrough before stopping the reused-counter cycle");
  expect(performance.tracks[0].endTick == 4, "the VM should stop when command, stack, and repeat-counter state recur");
}

void sequenceVmExecutesNestedCallInsideRepeat() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> callBytes{0xc0, 0x14, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x05, 0x04};
  const std::array<u8, 1> returnBytes{0xfd};
  addProbeCommand<ProbeCallCommand>(track, dialect, Address{0}, probeRange(0, callBytes.size()), callBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{3}, probeRange(3, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{20}, probeRange(20, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeReturnCommand>(track, dialect, Address{23}, probeRange(23, returnBytes.size()), returnBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
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
      .startAddress = Address{0},
  };

  const std::array<u8, 3> callBytes{0xc0, 0x14, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x05, 0x04};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x14, 0x00};
  const std::array<u8, 1> returnBytes{0xfd};
  addProbeCommand<ProbeCallCommand>(track, dialect, Address{0}, probeRange(0, callBytes.size()), callBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{3}, probeRange(3, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{20}, probeRange(20, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{23}, probeRange(23, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeReturnCommand>(track, dialect, Address{28}, probeRange(28, returnBytes.size()), returnBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
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
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 4> repeatBreakBytes{0xf1, 0x00, 0x0c, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatBreakCommand>(track, dialect, Address{3}, probeRange(3, repeatBreakBytes.size()),
                                           repeatBreakBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{7}, probeRange(7, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{12}, probeRange(12, endBytes.size()), endBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
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
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x14, 0x00};
  const std::array<u8, 4> repeatBreakBytes{0xf1, 0x00, 0x00, 0x00};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x02, 0x14, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{3}, probeRange(3, jumpBytes.size()), jumpBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{20}, probeRange(20, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatBreakCommand>(track, dialect, Address{23}, probeRange(23, repeatBreakBytes.size()),
                                           repeatBreakBytes);
  addProbeCommand<ProbeRepeatCommand>(track, dialect, Address{27}, probeRange(27, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(track, dialect, Address{32}, probeRange(32, endBytes.size()), endBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "repeat-break branch-to-visited fixture should not report diagnostics");
  expect(countProbeNotesAt(performance.tracks[0], 36) == 1,
         "repeat-break should execute a branch target even if that command ran earlier");
  expect(performance.tracks[0].endTick == 48,
         "repeat-break branch-to-visited fixture should stop only after the branch target note plays");
}

void sequenceVmPreservesLoopMarkersForInteriorJumpTarget() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x05, 0x00};
  addProbeCommand<ProbeProgramCommand>(track, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  const CommandId loopStartCommand =
      addProbeCommand<ProbeNoteCommand>(track, dialect, Address{5}, probeRange(5, noteBytes.size()), noteBytes);
  const CommandId jumpCommand =
      addProbeCommand<ProbeJumpCommand>(track, dialect, Address{8}, probeRange(8, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program);
  expect(performance.diagnostics.empty(), "interior loop target fixture should not report diagnostics");
  expect(performance.tracks[0].endTick == 24, "interior loop target should stop at the repeated command");
  const MarkerPerformanceEvent* loopStart = probeMarkerAt(performance.tracks[0], "Loop Start", 12);
  const MarkerPerformanceEvent* loopEnd = probeMarkerAt(performance.tracks[0], "Loop End", 24);
  expect(loopStart != nullptr && loopStart->header.sourceCommand == loopStartCommand,
         "preserve-loop marker should attach to the interior repeated command");
  expect(loopEnd != nullptr && loopEnd->header.sourceCommand == jumpCommand,
         "preserve-loop marker should attach to the jump into the decoded block");
}

void sequenceVmDoesNotWrapCommandAddressOverflow() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{std::numeric_limits<u64>::max() - 1},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x04};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{std::numeric_limits<u64>::max() - 1}, SourceRange{},
                                    noteBytes);
  // The decoded continuation is authoritative and must not wrap to the other
  // command merely because address + source size would overflow.
  track.commands.back().flow.continuation = Address{std::numeric_limits<u64>::max()};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{1}, SourceRange{}, noteBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.size() == 1 &&
             performance.diagnostics.front().message.find("continuation") != std::string::npos,
         "an unresolved authoritative continuation should produce one focused diagnostic");
  expect(performance.tracks[0].events.size() == 1 && performance.tracks[0].endTick == 4,
         "source-address fallthrough should not wrap around the address space");
}

SequenceDialect authoritativeFlowProbeDialect() {
  return SequenceDialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
}

SequenceRuntime authoritativeFlowProbeRuntime() {
  return SequenceRuntime{
              .execute =
                  [](const SourceCommand& command, std::any&, std::any&, PerformanceEmitter& out, VmApi& vm) {
                    if (command.opcode == 0xfe) {
                      return vm.jump(command.flow.continuation);
                    }
                    if (command.opcode == 0xfd) {
                      return vm.end();
                    }
                    if (command.opcode == 0xfc) {
                      return vm.fallthrough();
                    }
                    if (command.opcode != 0) {
                      out.note(command.opcode, 1.0, 1);
                    }
                    return Effects{};
                  },
  };
}

void sequenceVmUsesDecodedContinuationInsteadOfSizeOrStorageOrder() {
  const SequenceDialect dialect = authoritativeFlowProbeDialect();
  TrackProgram track{.startAddress = Address{100}};
  track.addCommand(Address{100}, 1, {}, {}, CommandFlow::fallthroughTo(Address{200}));
  track.addCommand(Address{101}, 99, {}, {}, CommandFlow::end(Address{102}));
  track.addCommand(Address{200}, 2, {}, {}, CommandFlow::end(Address{201}));

  const SequenceProgram program{
      .runtime = authoritativeFlowProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty() && performance.tracks[0].events.size() == 2,
         "an explicit continuation should execute without inferred-successor diagnostics");
  expect(std::get<NotePerformanceEvent>(performance.tracks[0].events[0]).key == 1.0 &&
             std::get<NotePerformanceEvent>(performance.tracks[0].events[1]).key == 2.0,
         "execution should follow the decoded continuation rather than encoded size or adjacent storage");
}

void sequenceVmUsesDecodedCallContinuationAsReturnAddress() {
  const SequenceDialect dialect = authoritativeFlowProbeDialect();
  TrackProgram track{.startAddress = Address{0}};
  track.addCommand(Address{0}, 0, {}, {}, CommandFlow::call(Address{10}, Address{20}));
  track.addCommand(Address{1}, 99, {}, {}, CommandFlow::end(Address{2}));
  track.addCommand(Address{10}, 10, {}, {}, CommandFlow::return_(Address{11}));
  track.addCommand(Address{20}, 20, {}, {}, CommandFlow::end(Address{21}));

  const SequenceProgram program{
      .runtime = authoritativeFlowProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty() && performance.tracks[0].events.size() == 2,
         "a call with a noncontiguous continuation should return without diagnostics");
  expect(std::get<NotePerformanceEvent>(performance.tracks[0].events[0]).key == 10.0 &&
             std::get<NotePerformanceEvent>(performance.tracks[0].events[1]).key == 20.0,
         "call return should use the decoded continuation rather than address plus encoded size");
}

void sequenceVmPreservesExplicitJumpToContinuation() {
  const SequenceDialect dialect = authoritativeFlowProbeDialect();
  TrackProgram track{.startAddress = Address{1}};
  track.addCommand(Address{1}, 1, {}, {}, CommandFlow::fallthroughTo(Address{0}));
  CommandFlow explicitJump = CommandFlow::fallthroughTo(Address{1});
  track.addCommand(Address{0}, 0xfe, {}, {}, std::move(explicitJump));

  const SequenceProgram program{
      .runtime = authoritativeFlowProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program);
  const auto notes = std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  const auto markers = std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<MarkerPerformanceEvent>(event);
  });
  expect(performance.diagnostics.empty() && notes == 1 && markers == 2,
         "a runtime jump to the continuation should remain explicit for loop detection");
}

void sequenceVmPrefersRuntimeFlowAndOtherwiseUsesTheDecodedDefault() {
  const SequenceDialect dialect = authoritativeFlowProbeDialect();
  const auto render = [&](u8 opcode, CommandFlow flow) {
    TrackProgram track{.startAddress = Address{0}};
    track.addCommand(Address{0}, opcode, {}, {}, std::move(flow));
    return SequenceVm().render(SequenceProgram{
        .runtime = authoritativeFlowProbeRuntime(),
            .timebase = dialect.timebase,
        .behavior = dialect.behavior,
            .tracks = {track},
    });
  };

  const PerformanceSequence fallback = render(0, CommandFlow::end(Address{1}));
  expect(fallback.diagnostics.empty(), "a command without a runtime transition should use its decoded default");

  const PerformanceSequence override = render(0xfd, CommandFlow::end(Address{1}));
  expect(override.diagnostics.empty(), "a runtime transition should be able to replace its decoded default");

  TrackProgram track{.startAddress = Address{0}};
  CommandFlow explicitFallthrough = CommandFlow::end(Address{1});
  track.addCommand(Address{0}, 0xfc, {}, {}, std::move(explicitFallthrough));
  track.addCommand(Address{1}, 1, {}, {}, CommandFlow::end(Address{2}));
  const PerformanceSequence fallthrough = SequenceVm().render(SequenceProgram{
      .runtime = authoritativeFlowProbeRuntime(),
          .timebase = dialect.timebase,
      .behavior = dialect.behavior,
          .tracks = {track},
  });
  expect(fallthrough.diagnostics.empty() && fallthrough.tracks[0].events.size() == 1,
         "an explicit runtime fallthrough should select the decoded continuation");
}

void sequenceVmReportsMissingJumpTargetAfterEmittedEvents() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x63, 0x00};
  addProbeCommand<ProbeNoteCommand>(track, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  const SourceRange jumpRange = probeRange(3, jumpBytes.size());
  addProbeCommand<ProbeJumpCommand>(track, dialect, Address{3}, jumpRange, jumpBytes);

  const SequenceProgram program{
      .runtime = probeSequenceRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program);
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
    return {};
  }
  if (command.address.value == 1) {
    return Effects::wait(command.opcode);
  }
  if (command.address.value == 10 || command.address.value == 11) {
    out.note(state.sharedValue, 1.0, 1);
    return Effects::wait(command.opcode);
  }
  return Effects{};
}

void sequenceVmSchedulesSemanticTracksAgainstOneProgramState() {
  const SequenceDialect dialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
  const SequenceRuntime runtime{
              .createProgramState = createScheduledProbeProgramState,
              .execute = executeScheduledProbe,
  };

  TrackProgram track0{.startAddress = Address{0}};
  track0.addCommand(Address{0}, 7, {}, {}, CommandFlow::fallthroughTo(Address{1}));
  track0.addCommand(Address{1}, 4, {}, {}, CommandFlow::fallthroughTo(Address{2}));
  track0.addCommand(Address{2}, 9, {}, {}, CommandFlow::fallthroughTo(Address{3}));
  track0.addCommand(Address{3}, 0, {}, {}, CommandFlow::end(Address{4}));

  TrackProgram track1{.sourceTrackNumber = 1, .startAddress = Address{10}};
  track1.addCommand(Address{10}, 2, {}, {}, CommandFlow::fallthroughTo(Address{11}));
  track1.addCommand(Address{11}, 0, {}, {}, CommandFlow::fallthroughTo(Address{12}));
  track1.addCommand(Address{12}, 0, {}, {}, CommandFlow::end(Address{13}));

  const SequenceProgram program{
      .runtime = runtime,
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track0, track1},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
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
      return Effects{};
    case 101:
      return Effects{};
    case 111:
      return Effects{};
    default:
      return Effects{};
  }
}

void sequenceVmCoordinatesSemanticLoopsAtSequenceScope() {
  const SequenceDialect dialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
  const SequenceRuntime runtime{.execute = executeScheduledLoopProbe};

  TrackProgram track0{.startAddress = Address{0}};
  track0.addCommand(Address{0}, 0, {}, {}, CommandFlow::fallthroughTo(Address{1}));
  track0.addCommand(Address{1}, 0, {}, {},
                       CommandFlow::jumpTo(Address{0}, Address{2}, JumpSemantics::LoopCandidate));

  // This track first jumps into an unvisited block. A per-track loop detector
  // stops track 0 too early while this track is still establishing its loop.
  TrackProgram track1{.startAddress = Address{100}};
  track1.addCommand(Address{100}, 0, {}, {}, CommandFlow::fallthroughTo(Address{101}));
  track1.addCommand(Address{101}, 0, {}, {},
                       CommandFlow::jumpTo(Address{110}, Address{102}, JumpSemantics::LoopCandidate));
  track1.addCommand(Address{110}, 0, {}, {}, CommandFlow::fallthroughTo(Address{111}));
  track1.addCommand(Address{111}, 0, {}, {},
                       CommandFlow::jumpTo(Address{110}, Address{112}, JumpSemantics::LoopCandidate));

  const SequenceProgram program{
      .runtime = runtime,
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track0, track1},
  };

  const PerformanceSequence playOnce = SequenceVm().render(program);
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
                                            .render(program);
  expect(oneRepeat.tracks[0].endTick == 12 && oneRepeat.tracks[1].endTick == 12,
         "one requested semantic loop should extend the longest sequence boundary once");
  expect(countProbeNotesAt(oneRepeat.tracks[0], 8) == 1 && countProbeNotesAt(oneRepeat.tracks[1], 8) == 1,
         "one requested semantic loop should replay every track together");
}

struct PlaylistProbeTrackState {
  u32 sectionsStarted = 0;
  u32 persistentValue = 0;

  void beginSection() { ++sectionsStarted; }
};

struct PlaylistProbePlayback {
  PlaylistProbeTrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
};

Effects executePlaylistProbe(const SourceCommand& command, std::any&, std::any& trackState, PerformanceEmitter& out,
                             VmApi& vm) {
  auto& state = std::any_cast<PlaylistProbeTrackState&>(trackState);
  if ((command.address.value & 1) != 0) {
    return Effects{};
  }

  ++state.persistentValue;
  out.noteOn(state.sectionsStarted * 10 + state.persistentValue, 1.0);
  return Effects::wait(command.opcode);
}

std::any createPlaylistProbeTrackState(const SequenceProgram&, const TrackProgram&) {
  return PlaylistProbeTrackState{};
}

void beginPlaylistProbeSection(std::any& trackState) {
  std::any_cast<PlaylistProbeTrackState&>(trackState).beginSection();
}

TrackProgram playlistProbeTrack(u32 trackId, std::initializer_list<std::pair<u32, u8>> sections) {
  TrackProgram track{
      .sourceTrackNumber = trackId,
      .startAddress = Address{sections.begin()->first},
  };
  for (const auto [address, duration] : sections) {
    track.addCommand(Address{address}, duration, {}, {}, CommandFlow::fallthroughTo(Address{address + 1}));
    track.addCommand(Address{address + 1}, 0, {}, {}, CommandFlow::endSection(Address{address + 2}));
  }
  return track;
}

SequenceDialect playlistProbeDialect() {
  return SequenceDialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
}

SequenceRuntime playlistProbeRuntime() {
  return SequenceRuntime{
              .createTrackState = createPlaylistProbeTrackState,
              .execute = executePlaylistProbe,
              .beginTrackSection = beginPlaylistProbeSection,
  };
}

void sequenceVmSwitchesParallelSectionsAtTheFirstChannelEnd() {
  const SequenceDialect dialect = playlistProbeDialect();
  const TrackProgram track0 = playlistProbeTrack(0, {{0, 8}});
  const TrackProgram track1 = playlistProbeTrack(1, {{100, 12}, {110, 4}});
  const SequenceProgram program{
      .runtime = playlistProbeRuntime(),
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {track0, track1},
      .sectionPlaylist =
          SectionPlaylist{
              .startAddress = Address{1000},
              .sections =
                  {
                      SequenceSection{.address = Address{500}, .trackStarts = {Address{0}, Address{100}}},
                      SequenceSection{.address = Address{600}, .trackStarts = {std::nullopt, Address{110}}},
                  },
              .commands =
                  {
                      PlaylistCommand{
                          .address = Address{1000},
                          .fallthrough = Address{1002},
                          .operation = PlaylistPlaySection{.section = Address{500}},
                      },
                      PlaylistCommand{
                          .address = Address{1002},
                          .fallthrough = Address{1004},
                          .operation = PlaylistPlaySection{.section = Address{600}},
                      },
                      PlaylistCommand{.address = Address{1004}, .operation = PlaylistEnd{}},
                  },
          },
  };

  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "parallel section fixture should render without diagnostics");
  expect(performance.tracks.size() == 2 && performance.tracks[0].endTick == 12 && performance.tracks[1].endTick == 12,
         "the earliest section end should restart every channel at one shared tick");
  const auto& firstLongNote = std::get<NotePerformanceEvent>(performance.tracks[1].events[0]);
  const auto& secondSectionNote = std::get<NotePerformanceEvent>(performance.tracks[1].events[1]);
  expect(firstLongNote.header.tick == 0 && firstLongNote.durationTicks == 8,
         "a section switch should trim a longer sibling channel at the boundary");
  expect(secondSectionNote.header.tick == 8 && secondSectionNote.durationTicks == 4 && secondSectionNote.key == 22.0,
         "track state should persist while the section-begin hook resets transient state");
}

void sequenceVmExecutesFiniteAndInfiniteSectionPlaylistRepeats() {
  const SequenceDialect dialect = playlistProbeDialect();
  const TrackProgram track = playlistProbeTrack(0, {{0, 4}});
  const auto makeProgram = [&](bool infinite) {
    return SequenceProgram{
        .runtime = playlistProbeRuntime(),
        .timebase = dialect.timebase,
        .behavior = dialect.behavior,
        .tracks = {track},
        .sectionPlaylist =
            SectionPlaylist{
                .startAddress = Address{1000},
                .sections = {SequenceSection{.address = Address{500}, .trackStarts = {Address{0}}}},
                .commands =
                    {
                        PlaylistCommand{
                            .address = Address{1000},
                            .fallthrough = Address{1002},
                            .operation = PlaylistPlaySection{.section = Address{500}},
                        },
                        PlaylistCommand{
                            .address = Address{1002},
                            .fallthrough = Address{1004},
                            .operation =
                                PlaylistRepeat{
                                    .additionalPlays = infinite ? 0u : 1u,
                                    .destination = Address{1000},
                                    .infinite = infinite,
                                },
                        },
                        PlaylistCommand{.address = Address{1004}, .operation = PlaylistEnd{}},
                    },
            },
    };
  };

  const PerformanceSequence finite = SequenceVm().render(makeProgram(false));
  expect(finite.tracks[0].endTick == 8 && finite.tracks[0].events.size() == 2,
         "a finite playlist repeat should play its destination the requested additional time");

  const PerformanceSequence playOnce = SequenceVm().render(makeProgram(true));
  expect(playOnce.tracks[0].endTick == 4 && playOnce.tracks[0].events.size() == 1,
         "an infinite playlist repeat should stop at its first loop boundary by default");

  const PerformanceSequence oneRepeat =
      SequenceVm(SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 1}).render(makeProgram(true));
  expect(oneRepeat.tracks[0].endTick == 8 && oneRepeat.tracks[0].events.size() == 2,
         "a requested playlist loop should replay every section once more");
}

void sequenceVmPairsNoteOnAndNoteOffCommands() {
  const SequenceDialect dialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
  const SequenceRuntime runtime{
              .execute =
                  [](const SourceCommand& command, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
                    switch (command.address.value) {
                      case 0:
                        out.noteOn(60, 1.0);
                        return Effects::wait(4);
                      case 1:
                        out.noteOn(64, 0.5);
                        return Effects::wait(2);
                      case 2:
                        out.sustainPedal(true);
                        return Effects::wait(1);
                      case 3:
                        out.noteOff(60);
                        return Effects::wait(3);
                      case 4:
                        out.noteOff(64);
                        return Effects::wait(1);
                      case 5:
                        out.sustainPedal(false);
                        return Effects::wait(2);
                      case 6:
                        out.noteOn(67, 0.75);
                        return Effects::wait(3);
                      case 7:
                        out.noteOn(67, 0.25);
                        return Effects::wait(2);
                      case 8:
                        out.noteOff(70);
                        return Effects::wait(1);
                      default:
                        return Effects{};
                    }
                  },
  };

  TrackProgram track{.startAddress = Address{0}};
  for (u32 address = 0; address < 9; ++address) {
    track.addCommand(Address{address}, 0, {}, {}, CommandFlow::fallthroughTo(Address{address + 1}),
                        SourceAnnotationId{100 + address});
  }
  track.addCommand(Address{9}, 0, {}, {}, CommandFlow::end(Address{10}), SourceAnnotationId{109});

  const PerformanceSequence performance = SequenceVm().render(SequenceProgram{
      .runtime = runtime,
          .timebase = dialect.timebase,
      .behavior = dialect.behavior,
          .tracks = {track},
  });
  expect(performance.diagnostics.empty(), "Note On/Off pairing should not invent diagnostics for unmatched offs");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 19,
         "Note On/Off pairing should retain the VM's ordinary track lifetime");

  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 4, "Note On/Off pairing should emit one duration event per attack");
  expect(notes[0]->key == 60.0 && notes[0]->header.tick == 0 && notes[0]->durationTicks == 11 &&
             notes[1]->key == 64.0 && notes[1]->header.tick == 4 && notes[1]->durationTicks == 7,
         "the sustain pedal should defer released notes until the pedal rises");
  expect(notes[2]->key == 67.0 && notes[2]->header.tick == 13 && notes[2]->durationTicks == 3 &&
             notes[3]->key == 67.0 && notes[3]->header.tick == 16 && notes[3]->durationTicks == 3,
         "a repeated Note On should end the prior same-key voice and track end should close its replacement");

  const auto spanEnd = [&](u32 annotation) -> u64 {
    const auto found =
        std::ranges::find(performance.sourceSpans, SourceAnnotationId{annotation}, &SourcePlaybackSpan::annotation);
    return found != performance.sourceSpans.end() ? found->endTick : 0;
  };
  expect(spanEnd(100) == 11 && spanEnd(101) == 11 && spanEnd(106) == 16 && spanEnd(107) == 19,
         "closing an active note should revise its Note On command's source playback span");
}

void sequenceVmClosesActiveNotesAtLoopCutoff() {
  const SequenceDialect dialect{
      .timebase = Timebase{.ppqn = 48},
      .behavior = SequenceProgramBehavior{},
  };
  const SequenceRuntime runtime{
              .execute =
                  [](const SourceCommand& command, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
                    if (command.address.value == 0) {
                      out.noteOn(72, 1.0);
                      return Effects::wait(4);
                    }
                    if (command.address.value == 100) {
                      out.noteOn(60, 1.0);
                      return Effects::wait(2);
                    }
                    return Effects{};
                  },
  };
  TrackProgram shortTrack{.startAddress = Address{100}};
  shortTrack.addCommand(Address{100}, 0, {}, {}, CommandFlow::fallthroughTo(Address{101}),
                           SourceAnnotationId{210});
  shortTrack.addCommand(Address{101}, 0, {}, {}, CommandFlow::end(Address{102}), SourceAnnotationId{211});

  TrackProgram loopTrack{.startAddress = Address{0}};
  loopTrack.addCommand(Address{0}, 0, {}, {}, CommandFlow::fallthroughTo(Address{1}), SourceAnnotationId{200});
  loopTrack.addCommand(Address{1}, 0, {}, {},
                          CommandFlow::jumpTo(Address{0}, Address{2}, JumpSemantics::LoopCandidate),
                          SourceAnnotationId{201});

  const PerformanceSequence performance = SequenceVm().render(SequenceProgram{
      .runtime = runtime,
      .timebase = dialect.timebase,
      .behavior = dialect.behavior,
      .tracks = {shortTrack, loopTrack},
  });
  const auto* shortNote = std::get_if<NotePerformanceEvent>(&performance.tracks[0].events.front());
  const auto* loopNote = std::get_if<NotePerformanceEvent>(&performance.tracks[1].events.front());
  expect(performance.tracks[0].endTick == 4 && performance.tracks[0].events.size() == 1 && shortNote != nullptr &&
             shortNote->durationTicks == 2,
         "an inactive track should close active notes at its own endpoint, not a later sequence cutoff");
  expect(performance.tracks[1].endTick == 4 && performance.tracks[1].events.size() == 1 && loopNote != nullptr &&
             loopNote->durationTicks == 4,
         "a play-once loop cutoff should close active notes and remove the boundary replay");
  const auto loopSpan =
      std::ranges::find(performance.sourceSpans, SourceAnnotationId{200}, &SourcePlaybackSpan::annotation);
  const auto shortSpan =
      std::ranges::find(performance.sourceSpans, SourceAnnotationId{210}, &SourcePlaybackSpan::annotation);
  expect(loopSpan != performance.sourceSpans.end() && loopSpan->endTick == 4 &&
             shortSpan != performance.sourceSpans.end() && shortSpan->endTick == 2,
         "loop cutoff should retain the finalized Note On source span");
}

}  // namespace

void runValueSequenceVmTests() {
  sequenceVmExecutesSourceCommandsAndStopsAtPlayOnceLoop();
  sequenceVmTimesCommandsThatEmitNoPerformanceEvents();
  sequenceVmPreservesPitchMotionThroughNoteRelease();
  sequenceVmReplaysInfiniteLoopsWhenRequested();
  sequenceVmStopsDeclaredLoopBeforeTargetReplay();
  sequenceVmPreservesDeclaredLoopAsPerformanceMarkers();
  sequenceVmLoopCandidateRequiresVisitedDestination();
  sequenceVmLoopCandidateIgnoresRepeatState();
  sequenceVmPreservesLoopCandidateAsPerformanceMarkers();
  sequenceVmPreservesLoopsAsPerformanceMarkers();
  sequenceVmUsesProgramCommandLimit();
  sequenceVmUsesInitialTempoAndGlobalEventOrder();
  sequenceVmFallsThroughBySourceAddressWhenDecodeOrderDiffers();
  sequenceVmEmitsProgramInitialChannelState();
  sequenceVmEmitsInitialMasterLevelOnce();
  sequenceVmExposesSubroutineStateFromItsCallStack();
  sequenceVmAllowsRepeatedCallsToSameSubroutine();
  sequenceVmReplaysFiniteRepeatBlocks();
  sequenceVmRepeatReplayUsesCommandAddressesNotSourceOffsets();
  sequenceVmDetectsCycleWhenRepeatCommandsReuseOneCounter();
  sequenceVmExecutesNestedCallInsideRepeat();
  sequenceVmExecutesRepeatInsideCall();
  sequenceVmRunsRepeatBreakSideEffectsOnlyWhenBranchTaken();
  sequenceVmRepeatBreakCanBranchToPreviouslyVisitedCode();
  sequenceVmPreservesLoopMarkersForInteriorJumpTarget();
  sequenceVmDoesNotWrapCommandAddressOverflow();
  sequenceVmUsesDecodedContinuationInsteadOfSizeOrStorageOrder();
  sequenceVmUsesDecodedCallContinuationAsReturnAddress();
  sequenceVmPreservesExplicitJumpToContinuation();
  sequenceVmPrefersRuntimeFlowAndOtherwiseUsesTheDecodedDefault();
  sequenceVmReportsMissingJumpTargetAfterEmittedEvents();
  sequenceVmSchedulesSemanticTracksAgainstOneProgramState();
  sequenceVmCoordinatesSemanticLoopsAtSequenceScope();
  sequenceVmSwitchesParallelSectionsAtTheFirstChannelEnd();
  sequenceVmExecutesFiniteAndInfiniteSectionPlaylistRepeats();
  sequenceVmPairsNoteOnAndNoteOffCommands();
  sequenceVmClosesActiveNotesAtLoopCutoff();
}

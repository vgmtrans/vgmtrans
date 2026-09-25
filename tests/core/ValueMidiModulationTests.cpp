/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>

using namespace vgmtrans::core;

namespace {

MidiSequence renderSimulatedModulation(u64 endTick, std::vector<PerformanceEvent> events) {
  // At 100 PPQN, this tempo makes each sequence tick ten milliseconds.
  events.insert(events.begin(), TempoPerformanceEvent{
                                    .header = PerformanceEventHeader{.tick = 0},
                                    .microsecondsPerQuarter = 1'000'000,
                                });
  return renderMidiSequence(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 100},
          .tracks = {PerformanceTrack{
              .id = TrackId{0},
              .sourceTrackNumber = 0,
              .endTick = endTick,
              .events = std::move(events),
          }},
      },
      MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
}

void performanceMidiRendererSimulatesDelayedVibratoAsPitchBendShape() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 8,
          .events =
              {
                  TempoPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .microsecondsPerQuarter = 1'000'000,
                  },
                  ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 0},
                                             .target = ModulationPerformanceTarget::VibratoDelay,
                                             .context = {.delay = LfoDelay{.ticks = 2}}},
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .amount = 1.0,
                      .context = LfoPerformanceContext{.frequencyHz = 12.5},
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .amount = 0.5,
                      .pitchDepthSemitones = 1.0,
                  },
              },
      }},
  };

  const MidiSequence midiSequence =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto& events = midiSequence.tracks[0].events;

  bool hasPreDelayNonzero = false;
  std::vector<std::pair<u64, s16>> pitchBends;
  for (const MidiEvent& event : events) {
    const auto* pitchBend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
    if (pitchBend == nullptr) {
      continue;
    }
    if (event.tick < 2 && pitchBend->value != 0) {
      hasPreDelayNonzero = true;
    }
    pitchBends.emplace_back(event.tick, pitchBend->value);
  }

  expect(!hasPreDelayNonzero, "sequence-event vibrato simulation should stay silent before the delay expires");
  const std::vector<std::pair<u64, s16>> expectedPitchBends{
      {2, 0}, {3, 2048}, {4, 4096}, {5, 2048}, {6, 0}, {7, -2048}, {8, -4096},
  };
  expect(pitchBends == expectedPitchBends,
         "sequence-event vibrato simulation should emit a delayed triangle LFO bend shape");
}

void performanceMidiRendererHonorsSpecifiedLfoWaveform() {
  std::vector<PerformanceEvent> events{
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoRate,
          .amount = 1.0,
          .context =
              LfoPerformanceContext{
                  .frequencyHz = 12.5,
                  .shape = LfoShape{.waveform = LfoWaveform::Sine},
              },
      },
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoDepth,
          .amount = 0.5,
          .pitchDepthSemitones = 1.0,
          .context = LfoPerformanceContext{.shape = LfoShape{.waveform = LfoWaveform::Sine}},
      },
      NotePerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .key = 60,
          .linearVelocity = 1.0,
          .durationTicks = 4,
      },
  };
  const MidiSequence midi = renderSimulatedModulation(4, std::move(events));
  const auto atTickTwo = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
    return bend != nullptr && event.tick == 2;
  });
  expect(atTickTwo != midi.tracks[0].events.end() &&
             midiChannelMessage(*atTickTwo, MidiChannelMessageKind::PitchBend)->value == 2896,
         "sequence-event simulation should evaluate an explicitly requested sine LFO");
}

void performanceMidiRendererHonorsSteppedLfoSamplesAndHeldDisableValue() {
  const LfoShape shape{.waveform = LfoWaveform::Sine, .samples = {0.0, 1.0, 0.0, -1.0}};
  std::vector<PerformanceEvent> events{
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoRate,
          .context =
              LfoPerformanceContext{
                  .frequencyHz = 25.0,
                  .shape = shape,
                  .sampleImmediatelyOnNote = true,
              },
      },
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 1.0,
          .context =
              LfoPerformanceContext{
                  .shape = shape,
                  .sampleImmediatelyOnNote = true,
              },
      },
      NotePerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .key = 60,
          .durationTicks = 4,
      },
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 1},
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 0.0,
          .context =
              LfoPerformanceContext{
                  .shape = shape,
                  .zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote,
              },
      },
      NotePerformanceEvent{
          .header = PerformanceEventHeader{.tick = 3},
          .key = 62,
          .durationTicks = 1,
          .restartsVibratoLfoPhase = false,
      },
  };
  const MidiSequence midi = renderSimulatedModulation(4, std::move(events));
  std::vector<std::pair<u64, s16>> bends;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      bends.emplace_back(event.tick, bend->value);
    }
  }
  expect(std::ranges::find(bends, std::pair<u64, s16>{1, 4096}) != bends.end() &&
             std::ranges::find(bends, std::pair<u64, s16>{3, 0}) != bends.end() &&
             std::ranges::none_of(bends, [](const auto& bend) { return bend.first == 2 && bend.second == 0; }),
         "a frozen lookup-table value should clear on the next note without restarting the oscillator");
}

void performanceMidiRendererReplacesSampledLfoWithNamedWaveform() {
  const LfoShape sampledShape{.waveform = LfoWaveform::Sine, .samples = {1.0, 1.0, 1.0, 1.0}};
  std::vector<PerformanceEvent> events{
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoRate,
          .context =
              LfoPerformanceContext{
                  .frequencyHz = 25.0,
                  .shape = sampledShape,
                  .sampleImmediatelyOnNote = true,
              },
      },
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 1.0,
          .context =
              LfoPerformanceContext{
                  .shape = sampledShape,
                  .sampleImmediatelyOnNote = true,
              },
      },
      NotePerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .key = 60,
          .durationTicks = 2,
      },
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 1},
          .target = ModulationPerformanceTarget::VibratoRate,
          .context =
              LfoPerformanceContext{
                  .frequencyHz = 25.0,
                  .shape = LfoShape{.waveform = LfoWaveform::Sine},
                  .sampleImmediatelyOnNote = true,
              },
      },
  };
  const MidiSequence midi = renderSimulatedModulation(2, std::move(events));
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                               return bend != nullptr && event.tick == 2 && bend->value == 0;
                             }),
         "a named waveform should replace, rather than inherit, an earlier exact sample table");
}

void performanceMidiRendererDoesNotDoubleDelayVibrato() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 8,
          .events =
              {
                  TempoPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .microsecondsPerQuarter = 1'000'000,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .amount = 0.0,
                      .pitchDepthSemitones = 0.0,
                  },
                  ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 0},
                                             .target = ModulationPerformanceTarget::VibratoDelay,
                                             .context = {.delay = LfoDelay{.ticks = 2}}},
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .amount = 1.0,
                      .context = LfoPerformanceContext{.frequencyHz = 12.5},
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .key = 60,
                      .linearVelocity = 1.0,
                      .durationTicks = 8,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 2},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .amount = 0.5,
                      .pitchDepthSemitones = 1.0,
                  },
              },
      }},
  };

  const MidiSequence midiSequence =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);

  std::vector<std::pair<u64, s16>> pitchBends;
  for (const MidiEvent& event : midiSequence.tracks[0].events) {
    if (const auto* pitchBend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      pitchBends.emplace_back(event.tick, pitchBend->value);
    }
  }

  const std::vector<std::pair<u64, s16>> expectedPitchBends{
      {0, 0}, {4, 2048}, {5, 4096}, {6, 2048}, {7, 0}, {8, -2048},
  };
  expect(pitchBends == expectedPitchBends,
         "sequence-event vibrato simulation should not apply a second delay to source-delayed depth envelopes");
}

void performanceMidiRendererRestartsSimulatedVibratoDelayForNewNotes() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 10,
          .events =
              {
                  TempoPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .microsecondsPerQuarter = 1'000'000,
                  },
                  ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 0},
                                             .target = ModulationPerformanceTarget::VibratoDelay,
                                             .context = {.delay = LfoDelay{.ticks = 2}}},
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .amount = 1.0,
                      .context = LfoPerformanceContext{.frequencyHz = 12.5},
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .amount = 0.5,
                      .pitchDepthSemitones = 1.0,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .key = 60,
                      .linearVelocity = 1.0,
                      .durationTicks = 5,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 5},
                      .key = 64,
                      .linearVelocity = 1.0,
                      .durationTicks = 5,
                  },
              },
      }},
  };

  const MidiSequence midiSequence =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);

  std::vector<std::pair<u64, s16>> pitchBends;
  for (const MidiEvent& event : midiSequence.tracks[0].events) {
    if (const auto* pitchBend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      pitchBends.emplace_back(event.tick, pitchBend->value);
    }
  }

  const std::vector<std::pair<u64, s16>> expectedPitchBends{
      {2, 0}, {3, 2048}, {4, 4096}, {5, 0}, {8, 2048}, {9, 4096}, {10, 2048},
  };
  expect(pitchBends == expectedPitchBends,
         "sequence-event vibrato simulation should restart the delay and phase for each new note");
}

void performanceMidiRendererClearsVibratoOutputWhenEventsRestartItsDelay() {
  for (const auto target : {ModulationPerformanceTarget::VibratoRate, ModulationPerformanceTarget::VibratoDepth}) {
    for (const bool milliseconds : {false, true}) {
      PerformanceTrack track{.id = TrackId{0}, .sourceTrackNumber = 0, .endTick = 8};
      u64 nextSequence = 0;
      u32 nextNote = 0;
      u32 nextAutomation = 0;
      PerformanceEmitter out{track,         {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote,
                             nextAutomation};
      out.tempo(1'000'000);
      out.note(60, 1.0, 8);
      out.pitchBend(0.25);
      const LfoPerformanceContext context{
          .cyclesPerTick = 0.125,
          .delay = milliseconds ? LfoDelay{.milliseconds = 20.0} : LfoDelay{.ticks = 2, .tempoRelative = true},
          .shape = LfoShape{.samples = {-1.0, -1.0, 1.0, 1.0}},
          .sampleImmediatelyOnNote = true,
          .restartMode = LfoRestartMode::PhaseAndDelay,
      };
      out.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 1.0,
          .context = context,
      });
      // Restart an already sounding oscillator without a note attack. Both
      // rate and depth events can restart the delay independently.
      out.at(4).modulation(ModulationPerformanceEvent{
          .target = target,
          .pitchDepthSemitones = 1.0,
          .context = context,
      });
      const MidiSequence midi =
          renderMidiSequence(PerformanceSequence{.timebase = Timebase{.ppqn = 100}, .tracks = {track}}, {},
                             ModulationConversionPolicy::SequenceEventSimulation);
      const auto bendAt = [&](u64 tick) {
        s32 value = 0;
        for (const auto& event : midi.tracks.front().events) {
          if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
              bend != nullptr && event.tick <= tick) {
            value = bend->value;
          }
        }
        return value;
      };
      expect(bendAt(3) == -3072, "the oscillator must produce a nonzero offset before its explicit restart");
      expect(bendAt(4) == 1024 && bendAt(5) == 1024,
             "restarting vibrato's delay must clear its old output while preserving the source pitch bend");
      expect(bendAt(6) == -3072, "the restarted oscillator must resume when its tick or physical delay expires");
    }
  }
}

void performanceMidiRendererReplacesSavedNoteDelay() {
  const LfoShape shape{.waveform = LfoWaveform::Sine, .samples = {0.0, 1.0, 0.0, -1.0}};
  std::vector<PerformanceEvent> events{
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoDelay,
          .context = {.delay = LfoDelay{.ticks = 3, .updateMode = LfoDelayUpdateMode::FutureNotesOnly}}},
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoRate,
          .context =
              LfoPerformanceContext{
                  .frequencyHz = 25.0,
                  .shape = shape,
                  .sampleImmediatelyOnNote = true,
              },
      },
      ModulationPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 1.0,
          .context =
              LfoPerformanceContext{
                  .shape = shape,
                  .sampleImmediatelyOnNote = true,
              },
      },
      NotePerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .key = 60,
          .durationTicks = 5,
      },
      ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 4},
                                 .target = ModulationPerformanceTarget::VibratoDelay,
                                 .context = {.delay = LfoDelay{.ticks = 1}}},
      NotePerformanceEvent{
          .header = PerformanceEventHeader{.tick = 5},
          .key = 62,
          .durationTicks = 3,
      },
  };
  const MidiSequence midi = renderSimulatedModulation(8, std::move(events));
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                               return bend != nullptr && event.tick == 7 && bend->value == 4096;
                             }),
         "an immediate-scope delay write should also replace the delay saved for later notes");
}

void performanceMidiRendererSimulatesTremoloUsingGlobalTempo() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks =
          {
              PerformanceTrack{
                  .id = TrackId{0},
                  .sourceTrackNumber = 0,
                  .endTick = 8,
                  .events = {TempoPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .microsecondsPerQuarter = 1'000'000,
                  }},
              },
              PerformanceTrack{
                  .id = TrackId{1},
                  .sourceTrackNumber = 1,
                  .endTick = 8,
                  .events =
                      {
                          ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 0},
                                                     .target = ModulationPerformanceTarget::TremoloDelay,
                                                     .context = {.delay = LfoDelay{.ticks = 2}}},
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::TremoloRate,
                              .amount = 1.0,
                              .context = LfoPerformanceContext{.frequencyHz = 25.0},
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::TremoloDepth,
                              .amount = 1.0,
                          },
                          NotePerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .key = 60,
                              .linearVelocity = 1.0,
                              .durationTicks = 8,
                          },
                          TempoPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 4},
                              .microsecondsPerQuarter = 1'000'000,
                          },
                      },
              },
          },
  };

  const MidiSequence midiSequence =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);

  std::vector<std::pair<u64, u8>> expressions;
  for (const MidiEvent& event : midiSequence.tracks[1].events) {
    if (const auto* expression = midiController(event, MidiController::Expression)) {
      expressions.emplace_back(event.tick, expression->value);
    }
  }
  const std::vector<std::pair<u64, u8>> expectedExpressions{
      {2, 127}, {3, 110}, {4, 90}, {5, 110}, {6, 127}, {7, 110}, {8, 90},
  };
  expect(expressions == expectedExpressions,
         "sequence-event tremolo should use a delayed LFO instead of static attenuation");
  expect(std::ranges::count_if(midiSequence.tracks[0].events,
                               [](const MidiEvent& event) { return midiMeta(event, 81) != nullptr; }) == 1 &&
             std::ranges::none_of(midiSequence.tracks[1].events,
                                  [](const MidiEvent& event) { return midiMeta(event, 81) != nullptr; }),
         "global tempo output should be written once on the first MIDI track");
}

void performanceMidiRendererUsesGlobalTempoOrderAtTrackBoundaries() {
  const PerformanceSequence performance{
      .initialTempoMicrosecondsPerQuarter = 1000000,
      .tracks =
          {
              PerformanceTrack{
                  .id = TrackId{0},
                  .endTick = 20,
                  .events =
                      {
                          TempoPerformanceEvent{.header = {.track = TrackId{0}, .tick = 12, .sequence = 10},
                                                .microsecondsPerQuarter = 250000},
                          TempoPerformanceEvent{.header = {.track = TrackId{0}, .tick = 30, .sequence = 12},
                                                .microsecondsPerQuarter = 500000},
                      }},
              PerformanceTrack{
                  .id = TrackId{1},
                  .endTick = 40,
                  .events =
                      {
                          TempoPerformanceEvent{.header = {.track = TrackId{1}, .tick = 12, .sequence = 1},
                                                .microsecondsPerQuarter = 750000},
                          TempoPerformanceEvent{.header = {.track = TrackId{1}, .tick = 24, .sequence = 11},
                                                .microsecondsPerQuarter = 250000},
                      }},
          },
  };
  const auto midi = renderMidiSequence(performance);
  std::vector<std::pair<u64, u32>> tempos;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto tempo = midiTempo(event)) {
      tempos.emplace_back(event.tick, *tempo);
    }
  }
  expect(tempos == std::vector<std::pair<u64, u32>>{{0, 1000000}, {12, 750000}, {12, 250000}, {30, 500000}},
         "MIDI must use global tempo order, including initial tempo and deduplication across tracks");
  expect(PerformanceTempoMap{performance}.microsecondsPerQuarterAt(12) == tempos[2].second,
         "the last MIDI tempo at a shared tick must match the tempo used for physical duration calculations");
  expect(midi.tracks[0].endTick == 30 &&
             std::ranges::none_of(midi.tracks[1].events,
                                  [](const MidiEvent& event) { return midiMeta(event, 0x51) != nullptr; }),
         "the conductor track must cover all global tempos and remain their only output location");
}

void performanceMidiRendererHonorsNoBoostTremoloPhaseAndResetPolicy() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 7,
          .events =
              {
                  TempoPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .microsecondsPerQuarter = 1'000'000,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::TremoloRate,
                      .context =
                          LfoPerformanceContext{
                              .frequencyHz = 25.0,
                              .shape = LfoShape{.waveform = LfoWaveform::Triangle},
                              .initialPhaseCycles = 0.75,
                          },
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::TremoloDepth,
                      .volumeDepthDecibels = 3.0,
                      .context =
                          LfoPerformanceContext{
                              .shape = LfoShape{.waveform = LfoWaveform::Triangle},
                              .initialPhaseCycles = 0.75,
                              .tremoloGainMode = TremoloGainMode::NoBoost,
                          },
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .key = 60,
                      .durationTicks = 2,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 2},
                      .key = 62,
                      .durationTicks = 2,
                      .restartsLfoPhase = false,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 4},
                      .key = 64,
                      .durationTicks = 2,
                      .restartsLfoPhase = false,
                      .restartsTremoloLfoPhase = true,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 6},
                      .key = 65,
                      .durationTicks = 1,
                      .restartsTremoloLfoPhase = false,
                  },
              },
      }},
  };

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  std::vector<std::pair<u64, u8>> expressions;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* expression = midiController(event, MidiController::Expression)) {
      expressions.emplace_back(event.tick, expression->value);
    }
  }

  expect(!expressions.empty() && expressions.front() == std::pair<u64, u8>{0, 90},
         "no-boost tremolo should begin at its six-decibel trough without exceeding nominal gain");
  expect(std::ranges::find(expressions, std::pair<u64, u8>{2, 107}) != expressions.end() &&
             std::ranges::find(expressions, std::pair<u64, u8>{2, 90}) == expressions.end(),
         "a note that preserves LFO phase should continue the existing tremolo curve");
  expect(std::ranges::find(expressions, std::pair<u64, u8>{4, 90}) != expressions.end(),
         "a target-specific reset should override the note's legacy preserve policy");
  expect(std::ranges::find(expressions, std::pair<u64, u8>{6, 90}) == expressions.end(),
         "a target-specific preserve should override the note's legacy reset policy");
  expect(std::ranges::all_of(expressions, [](const auto& expression) { return expression.second <= 127; }),
         "no-boost tremolo should never emit expression above nominal gain");
}

}  // namespace

void runValueMidiModulationTests() {
  performanceMidiRendererSimulatesDelayedVibratoAsPitchBendShape();
  performanceMidiRendererHonorsSpecifiedLfoWaveform();
  performanceMidiRendererHonorsSteppedLfoSamplesAndHeldDisableValue();
  performanceMidiRendererReplacesSampledLfoWithNamedWaveform();
  performanceMidiRendererDoesNotDoubleDelayVibrato();
  performanceMidiRendererRestartsSimulatedVibratoDelayForNewNotes();
  performanceMidiRendererClearsVibratoOutputWhenEventsRestartItsDelay();
  performanceMidiRendererReplacesSavedNoteDelay();
  performanceMidiRendererSimulatesTremoloUsingGlobalTempo();
  performanceMidiRendererUsesGlobalTempoOrderAtTrackBoundaries();
  performanceMidiRendererHonorsNoBoostTremoloPhaseAndResetPolicy();
}

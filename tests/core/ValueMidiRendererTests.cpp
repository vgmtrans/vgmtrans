/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"

#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>

using namespace vgmtrans::core;

namespace {

void performanceMidiRendererTrustsSourceNoteExtensions() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 2,
          .endTick = 30,
          .events =
              {
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .key = 60.0,
                      .linearVelocity = 0.75,
                      .durationTicks = 12,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 12},
                      .key = 60.0,
                      .linearVelocity = 0.5,
                      .durationTicks = 6,
                      .extendsPrevious = true,
                  },
                  GlobalTransposePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 18},
                      .semitones = -1,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 18},
                      .key = 60.0,
                      .linearVelocity = 0.5,
                      .durationTicks = 6,
                      .extendsPrevious = true,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 24},
                      .key = 62.0,
                      .linearVelocity = 0.5,
                      .durationTicks = 6,
                  },
                  TimeSignaturePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 30},
                      .numerator = 3,
                      .denominator = 4,
                      .clocksPerMetronomeClick = 48,
                  },
              },
      }},
  };

  const MidiSequence midiSequence = renderMidiSequence(performance);
  expect(midiSequence.tracks.size() == 1, "performance renderer should preserve tracks");
  const auto& events = midiSequence.tracks[0].events;
  expect(midiPort(events[0]).has_value(), "performance renderer should mark each track's MIDI port");
  const auto firstNote = midiNote(events[1]);
  const auto secondNote = midiNote(events[2]);
  expect(firstNote != nullptr && events[1].tick == 0 && firstNote->key == 60 && firstNote->duration == 24,
         "performance renderer should trust source-selected note extensions");
  expect(secondNote != nullptr && events[2].tick == 24 && secondNote->key == 61 && secondNote->duration == 6,
         "performance renderer should emit a new note when the source does not request an extension");
  const auto* timeSignature = midiMeta(events[3], 0x58);
  expect(timeSignature != nullptr && events[3].tick == 30 && timeSignature->data[0] == 3,
         "performance renderer should preserve source time signatures");
  expect(midiSequence.tracks[0].endTick == 30, "performance renderer should preserve track end ticks");
}

void performanceMidiRendererKeepsPhysicalLimitsAcrossPortamentoFragments() {
  for (const auto mode : {MidiPitchTransitionRendering::PitchBend, MidiPitchTransitionRendering::Portamento}) {
    for (const u64 slideTick : {4, 20}) {
      for (const double limitMilliseconds : {0.0, 1000.0}) {
        PerformanceTrack track{.id = TrackId{0}, .endTick = 44};
        u64 nextSequence = 0;
        u32 nextNote = 0;
        u32 nextAutomation = 0;
        PerformanceEmitter out{
            track, {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
        const auto held = out.note(
            NotePerformanceEvent{.key = 60, .durationTicks = 40, .maximumDurationMilliseconds = limitMilliseconds});
        out.at(8).tempo(1000000);
        out.at(slideTick).pitchSlide(held, 60, 64, 8);
        out.at(40).note(67, 1.0, 4);
        const PerformanceSequence performance{.timebase = {.ppqn = 10}, .tracks = {track}};
        const auto midi = renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = mode});
        const auto notes = midiNotes(midi.tracks[0].events);
        // At 50 ms/tick through tick 8, then 100 ms/tick, the timer expires at 14.
        const u64 stopTick = limitMilliseconds == 0.0 ? 0 : 14;
        const bool split = mode == MidiPitchTransitionRendering::Portamento && slideTick < stopTick;
        expect(notes.size() == (split ? 3 : 2) && notes.front().tick == 0 &&
                   notes.front().duration == (split ? 5 : stopTick),
               "a hardware timer must survive portamento splitting, including a zero-duration source attack");
        if (split) {
          expect(notes[1].tick == 4 && notes[1].duration == 10,
                 "the destination fragment must inherit the source attack's stop time across tempo changes");
        }
        expect(notes.back().tick == 40 && notes.back().key == 67 && notes.back().duration == 4,
               "a genuine new attack must reset the previous voice's duration limit");
        const auto& source = std::get<NotePerformanceEvent>(performance.tracks[0].events.front());
        expect(source.durationTicks == 40 && source.maximumDurationMilliseconds == limitMilliseconds,
               "MIDI duration limits must leave the source performance intact");
      }
    }
  }
}

void performanceMidiRendererKeepsPhysicalLimitsAcrossVoiceContinuations() {
  for (const auto mode : {MidiPitchTransitionRendering::PitchBend, MidiPitchTransitionRendering::Portamento}) {
    for (const bool changesKey : {false, true}) {
      for (const bool laterLimit : {false, true}) {
        PerformanceTrack track{.id = TrackId{0}, .endTick = 40};
        u64 nextSequence = 0;
        u32 nextNote = 0;
        u32 nextAutomation = 0;
        PerformanceEmitter out{
            track, {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
        const auto held = out.note(
            NotePerformanceEvent{.key = 60,
                                 .durationTicks = laterLimit ? 40u : 4u,
                                 .maximumDurationMilliseconds = laterLimit ? std::nullopt : std::optional{1000.0}});
        if (laterLimit) {
          out.at(2).pitchSlide(held, 60, 62, 2).portamentoOverlap(24);
        }
        const auto next = out.at(4).note(
            NotePerformanceEvent{.key = changesKey ? 64.0 : 60.0,
                                 .durationTicks = 36,
                                 .maximumDurationMilliseconds = laterLimit ? std::optional{1000.0} : std::nullopt,
                                 .extendsPrevious = !changesKey});
        if (changesKey) {
          out.at(4).pitchSlide(next, laterLimit ? 62 : 60, 64, 8).continueFrom(held).portamentoOverlap(24);
        }
        out.at(8).tempo(1000000);
        const auto midi = renderMidiSequence(PerformanceSequence{.timebase = {.ppqn = 10}, .tracks = {track}},
                                             MidiExportOptions{.pitchTransitions = mode});
        const auto notes = midiNotes(midi.tracks[0].events);
        const u64 stopTick = laterLimit ? 16 : 14;
        expect(!notes.empty() &&
                   std::ranges::all_of(
                       notes, [stopTick](const MidiNoteView& note) { return note.tick + note.duration <= stopTick; }) &&
                   notes.back().tick + notes.back().duration == stopTick,
               "continuations must honor inherited and newly shortened hardware limits on every live fragment");
        if ((!changesKey && !laterLimit) || mode == MidiPitchTransitionRendering::PitchBend) {
          expect(notes.size() == 1 && notes[0].tick == 0 && notes[0].duration == stopTick,
                 "same-voice MIDI extensions must stop at the hardware deadline");
        }
      }
    }
  }
}

void performanceMidiRendererLimitsOnlyTheOwningVoice() {
  for (const auto mode : {MidiPitchTransitionRendering::PitchBend, MidiPitchTransitionRendering::Portamento}) {
    for (const bool linkedNote : {false, true}) {
      for (const u32 lane : {0, 1}) {
        PerformanceTrack track{.id = TrackId{0}, .endTick = 105};
        u64 nextSequence = 0;
        u32 nextNote = 0;
        u32 nextAutomation = 0;
        PerformanceEmitter out{
            track, {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
        const auto held = out.note(NotePerformanceEvent{
            .key = 60, .durationTicks = linkedNote ? 10u : 100u, .maximumDurationMilliseconds = 100.0});
        out.at(5).note(NotePerformanceEvent{.key = 72, .durationTicks = 100, .lane = PerformanceLaneId{lane}});
        if (linkedNote) {
          const auto next = out.at(10).note(64, 1.0, 90);
          out.at(10).pitchSlide(next, 60, 64, 10).continueFrom(held);
        } else {
          out.at(10).pitchSlide(held, 60, 64, 10);
        }
        const auto midi = renderMidiSequence(PerformanceSequence{.timebase = {.ppqn = 100}, .tracks = {track}},
                                             {.pitchTransitions = mode});
        const auto notes = midiNotes(midi.tracks[0].events);
        const bool split = mode == MidiPitchTransitionRendering::Portamento;
        expect(notes.size() == (split ? 3 : 2) && notes[0].tick == 0 && notes[0].duration == (split ? 11 : 20) &&
                   notes[1].tick == 5 && notes[1].key == 72 && notes[1].duration == 100,
               "a voice's hardware deadline and extensions must leave an unrelated overlapping note intact");
        if (split) {
          expect(notes[2].tick == 10 && notes[2].duration == 10,
                 "a native-portamento fragment must inherit its own voice's absolute stop time");
        }
      }
    }
  }
}

void performanceMidiRendererSelectsTuningRepresentation() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .endTick = 18,
          .events =
              {
                  TuningPerformanceEvent{.header = PerformanceEventHeader{.tick = 0}, .cents = 100.0},
                  NotePerformanceEvent{.header = PerformanceEventHeader{.tick = 0},
                                       .key = 60.0,
                                       .linearVelocity = 1.0,
                                       .durationTicks = 18},
                  PitchBendPerformanceEvent{.header = PerformanceEventHeader{.tick = 6}, .semitones = 0.5},
                  TuningPerformanceEvent{.header = PerformanceEventHeader{.tick = 12}, .cents = 214.0625},
                  TuningPerformanceEvent{.header = PerformanceEventHeader{.tick = 18}, .cents = 14.0625},
              },
      }},
  };

  const MidiSequence pitchBend = renderMidiSequence(performance);
  std::vector<std::pair<u64, s32>> tuningBends;
  size_t tuningRpnCount = 0;
  for (const auto& rpn : midiRpns(pitchBend.tracks.front().events)) {
    tuningRpnCount += rpn.parameterMsb == 0 && (rpn.parameterLsb == 1 || rpn.parameterLsb == 2);
  }
  for (const auto& event : pitchBend.tracks.front().events) {
    if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      tuningBends.emplace_back(event.tick, bend->value);
    }
  }
  expect(tuningRpnCount == 0 &&
             midiPitchBendRanges(pitchBend.tracks.front().events) == std::vector<std::pair<u64, u16>>{{0, 300}},
         "default tuning should reserve one stable pitch-bend range without tuning RPNs");
  expect(tuningBends == std::vector<std::pair<u64, s32>>{{0, 2731}, {6, 4096}, {12, 7211}, {18, 1749}},
         "default tuning should place the complete tuning and source bend on the wheel");

  MidiExportOptions coarseOptions;
  coarseOptions.tuning = MidiTuningRendering::CoarseAndFineTune;
  const MidiSequence coarse = renderMidiSequence(performance, coarseOptions);
  std::vector<u16> coarseTuning;
  std::vector<u16> coarseFine;
  std::vector<std::pair<u64, s32>> coarseBends;
  for (const auto& rpn : midiRpns(coarse.tracks.front().events)) {
    if (rpn.parameterMsb == 0 && rpn.parameterLsb == 2) {
      coarseTuning.push_back(rpn.value);
    } else if (rpn.parameterMsb == 0 && rpn.parameterLsb == 1) {
      coarseFine.push_back(rpn.value);
    }
  }
  for (const auto& event : coarse.tracks.front().events) {
    if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      coarseBends.emplace_back(event.tick, bend->value);
    }
  }
  expect(coarseTuning == std::vector<u16>{8320, 8448, 8192} && coarseFine == std::vector<u16>{8192, 9344, 9344},
         "coarse tuning mode should retain the standards-oriented RPN representation");
  expect(coarseBends == std::vector<std::pair<u64, s32>>{{6, 2048}},
         "coarse tuning mode should leave the source pitch-bend lane unchanged");
}

void performanceMidiRendererWritesTimeSignaturesToFirstTrack() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              PerformanceTrack{
                  .id = TrackId{0},
                  .sourceTrackNumber = 0,
                  .endTick = 12,
                  .events =
                      {
                          NotePerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .key = 60.0,
                              .linearVelocity = 0.5,
                              .durationTicks = 12,
                          },
                      },
              },
              PerformanceTrack{
                  .id = TrackId{1},
                  .sourceTrackNumber = 12,
                  .endTick = 48,
                  .events =
                      {
                          TimeSignaturePerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 48},
                              .numerator = 4,
                              .denominator = 4,
                              .clocksPerMetronomeClick = 48,
                          },
                      },
              },
          },
  };

  const MidiSequence midiSequence = renderMidiSequence(performance);
  expect(midiSequence.tracks.size() == 2, "performance renderer should preserve source track count");

  const auto& firstTrackEvents = midiSequence.tracks[0].events;
  const auto& secondTrackEvents = midiSequence.tracks[1].events;
  const auto* timeSignature = midiMeta(firstTrackEvents.back(), 0x58);
  expect(timeSignature != nullptr && firstTrackEvents.back().tick == 48 && timeSignature->data[0] == 4,
         "performance renderer should write global time signatures to the first MIDI track");
  expect(midiSequence.tracks[0].endTick == 48, "first MIDI track end should cover global time signatures");
  expect(std::none_of(secondTrackEvents.begin(), secondTrackEvents.end(),
                      [](const MidiEvent& event) { return midiMeta(event, 88) != nullptr; }),
         "performance renderer should not duplicate time signatures on their source track");
}

void performanceMidiRendererUsesGlobalExecutionOrderForTransposeAndMeter() {
  for (const u64 otherSequence : {1u, 10u}) {
    const PerformanceSequence performance{
        .tracks =
            {
                PerformanceTrack{
                    .id = TrackId{0},
                    .endTick = 13,
                    .events =
                        {
                            GlobalTransposePerformanceEvent{.header = {.tick = 12, .sequence = 10}, .semitones = 5},
                            TimeSignaturePerformanceEvent{
                                .header = {.tick = 12, .sequence = 10}, .numerator = 3, .denominator = 4},
                            NotePerformanceEvent{.header = {.tick = 12, .sequence = 11}, .key = 60, .durationTicks = 1},
                        }},
                PerformanceTrack{.id = TrackId{1},
                                 .endTick = 12,
                                 .events =
                                     {
                                         GlobalTransposePerformanceEvent{
                                             .header = {.track = TrackId{1}, .tick = 12, .sequence = otherSequence},
                                             .semitones = -3},
                                         TimeSignaturePerformanceEvent{
                                             .header = {.track = TrackId{1}, .tick = 12, .sequence = otherSequence},
                                             .numerator = 4,
                                             .denominator = 4},
                                     }},
            },
    };
    const auto midi = renderMidiSequence(performance);
    const auto notes = midiNotes(midi.tracks[0].events);
    expect(notes.size() == 1 && notes[0].key == (otherSequence == 1 ? 65 : 57),
           "global transposition must follow execution order, preserving track order only for equal sequence values");
    std::vector<u8> meters;
    for (const auto& event : midi.tracks[0].events) {
      if (const auto* signature = midiMeta(event, 0x58)) {
        meters.push_back(signature->data[0]);
      }
    }
    expect(
        meters == (otherSequence == 1 ? std::vector<u8>{4, 3} : std::vector<u8>{3, 4}) &&
            std::ranges::none_of(midi.tracks[1].events, [](const MidiEvent& event) { return midiMeta(event, 0x58); }),
        "the conductor's time signatures must use the same stable global execution order");
  }
}

void performanceMidiRendererWritesPanGainResetWhenRequested() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 24,
          .events =
              {
                  StereoBalancePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .leftGain = 0.5,
                      .rightGain = 0.0,
                  },
                  StereoBalancePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 12},
                      .leftGain = 1.0,
                      .rightGain = 1.0,
                  },
                  StereoBalancePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 24},
                      .leftGain = 0.5,
                      .rightGain = -0.5,
                  },
              },
      }},
  };

  const MidiSequence midiSequence = renderMidiSequence(performance);
  const auto& events = midiSequence.tracks[0].events;
  expect(midiController(events[1], MidiController::Pan)->value == 0 &&
             midiController(events[2], MidiController::ChannelVolume)->value == 76,
         "pan gain compensation should use bounded channel-volume headroom");
  expect(midiController(events[3], MidiController::Pan)->value == 64 &&
             midiController(events[4], MidiController::ChannelVolume)->value == 127,
         "maximum pan compensation should fit exactly within reserved headroom");
  expect(midiController(events[5], MidiController::Pan)->value == 64 &&
             midiController(events[6], MidiController::ChannelVolume)->value == 90,
         "MIDI pan lowering should preserve a phase-inverted channel's magnitude");
}

void performanceMidiRendererKeepsPanGainOutOfExpression() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 72,
          .events =
              {
                  ExpressionPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .linearGain = 0.25,
                  },
                  StereoBalancePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 12},
                      .leftGain = 0.5,
                      .rightGain = 0.0,
                  },
                  StereoBalancePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 24},
                      .leftGain = 1.0,
                      .rightGain = 0.0,
                  },
                  PanPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 36},
                      .stereoPosition = 0.0,
                      .law = PanLaw::EqualPower,
                      .linearGain = 0.25,
                  },
                  PanPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 48},
                      .law = PanLaw::EqualPower,
                  },
                  PanPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 60},
                      .law = PanLaw::EqualPower,
                      .linearGain = 0.25,
                  },
                  PanPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 72},
                      .law = PanLaw::EqualPower,
                      .linearGain = 1.0,
                  },
              },
      }},
  };

  const auto controllerValues = [&](ModulationConversionPolicy policy, MidiController controller) {
    const MidiSequence midi = renderMidiSequence(performance, MidiExportOptions{}, policy);
    std::vector<u8> values;
    for (const MidiEvent& event : midi.tracks[0].events) {
      if (const auto* message = midiController(event, controller)) {
        values.push_back(message->value);
      }
    }
    return values;
  };

  const std::vector<u8> expectedVolume{90, 127, 64, 127, 64, 127};
  for (const auto policy :
       {ModulationConversionPolicy::SynthModulators, ModulationConversionPolicy::SequenceEventSimulation}) {
    expect(controllerValues(policy, MidiController::Expression) == std::vector<u8>{64},
           "pan compensation should not rewrite source expression");
    expect(controllerValues(policy, MidiController::ChannelVolume) == expectedVolume,
           "pan gain must compose with channel volume and reset for both omitted and explicit unit gain");
  }

  PerformanceSequence precisePerformance = performance;
  std::get<ExpressionPerformanceEvent>(precisePerformance.tracks[0].events[0]).sourceQuantization =
      ValueQuantization{.levels = 256};
  const MidiSequence preciseMidi = renderMidiSequence(precisePerformance);
  expect(
      std::count_if(preciseMidi.tracks[0].events.begin(), preciseMidi.tracks[0].events.end(),
                    [](const MidiEvent& event) { return isMidiControllerLsb(event, MidiController::Expression); }) == 1,
      "only source expression should retain source expression quantization");
}

void performanceMidiRendererLowersDeclaredPanLaws() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              PerformanceTrack{
                  .id = TrackId{0},
                  .events =
                      {
                          PanPerformanceEvent{
                              .stereoPosition = 0.0,
                              .law = PanLaw::EqualPower,
                          },
                      },
              },
              PerformanceTrack{
                  .id = TrackId{1},
                  .events =
                      {
                          PanPerformanceEvent{
                              .stereoPosition = 0.0,
                              .law = PanLaw::ConstantSum,
                          },
                      },
              },
          },
  };

  const MidiSequence midi = renderMidiSequence(performance);
  expect(
      std::ranges::none_of(midi.tracks[0].events,
                           [](const MidiEvent& event) { return isMidiController(event, MidiController::Expression); }),
      "equal-power positional pan should not add loudness compensation");
  const auto constantSumVolume = std::ranges::find_if(midi.tracks[1].events, [](const MidiEvent& event) {
    return isMidiController(event, MidiController::ChannelVolume);
  });
  expect(constantSumVolume != midi.tracks[1].events.end() &&
             midiController(*constantSumVolume, MidiController::ChannelVolume)->value == 107,
         "constant-sum center pan should retain its lower combined gain when lowered to MIDI equal-power pan");
}

void performanceMidiRendererRetainsPanLawDuringLfoSimulation() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .endTick = 2,
          .events =
              {
                  PanPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 0},
                      .stereoPosition = 0.0,
                      .law = PanLaw::ConstantSum,
                      .linearGain = 255.0 / 256.0,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 1},
                      .target = ModulationPerformanceTarget::PanRate,
                      .context =
                          LfoPerformanceContext{
                              .cyclesPerTick = 0.25,
                              .shape = LfoShape{.waveform = LfoWaveform::Triangle},
                              .panLaw = PanLaw::ConstantSum,
                          },
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 2},
                      .target = ModulationPerformanceTarget::PanDepth,
                      .panDepth = 1.0,
                      .context =
                          LfoPerformanceContext{
                              .cyclesPerTick = 0.25,
                              .shape = LfoShape{.waveform = LfoWaveform::Triangle},
                              .panLaw = PanLaw::ConstantSum,
                          },
                  },
              },
      }},
  };

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto& events = midi.tracks[0].events;
  expect(std::ranges::any_of(events,
                             [](const MidiEvent& event) {
                               const auto* pan = midiController(event, MidiController::Pan);
                               return pan && event.tick == 2 && pan->value == 127;
                             }) &&
             std::ranges::any_of(events,
                                 [](const MidiEvent& event) {
                                   const auto* volume = midiController(event, MidiController::ChannelVolume);
                                   return volume && event.tick == 2 && volume->value == 127;
                                 }),
         "constant-sum pan LFO simulation should restore full aggregate gain at a hard-pan peak");
}

void performanceMidiRendererHonorsMidiExportOptions() {
  PerformanceSequence performance{.timebase = Timebase{.ppqn = 48},
                                  .tracks = {
                                      PerformanceTrack{
                                          .id = TrackId{0},
                                          .sourceTrackNumber = 0,
                                          .events =
                                              {
                                                  InstrumentPerformanceEvent{
                                                      .header = PerformanceEventHeader{.tick = 0},
                                                      .instrument = InstrumentAddress{.bank = 130, .program = 5},
                                                  },
                                                  LevelPerformanceEvent{
                                                      .header = PerformanceEventHeader{.tick = 0},
                                                      .linearGain = 1.0,
                                                      .sourceQuantization = ValueQuantization{.levels = 256},
                                                  },
                                                  ExpressionPerformanceEvent{
                                                      .header = PerformanceEventHeader{.tick = 0},
                                                      .linearGain = 1.0,
                                                      .sourceQuantization = ValueQuantization{.levels = 256},
                                                  },
                                              },
                                      },
                                  }};
  for (u32 trackIndex = 1; trackIndex <= 15; ++trackIndex) {
    performance.tracks.push_back(PerformanceTrack{
        .id = TrackId{trackIndex},
        .sourceTrackNumber = trackIndex,
        .events = {NotePerformanceEvent{
            .header = PerformanceEventHeader{.tick = 0},
            .key = 60.0,
            .linearVelocity = 1.0,
            .durationTicks = 1,
        }},
    });
  }

  const MidiSequence autoMidi = renderMidiSequence(performance);
  expect(*midiPort(autoMidi.tracks[0].events[0]) == 0,
         "MIDI renderer should emit port zero for the first channel group");
  expect(midiBankSelect(autoMidi.tracks[0].events[1])->bank == 130 &&
             !midiBankSelect(autoMidi.tracks[0].events[1])->writeLsb,
         "MIDI renderer should retain logical banks for MSB-only bank select by default");
  expect(isMidiController(autoMidi.tracks[0].events[3], MidiController::ChannelVolume) &&
             isMidiControllerLsb(autoMidi.tracks[0].events[4], MidiController::ChannelVolume),
         "MIDI renderer should honor source volume quantization by default");
  expect(isMidiController(autoMidi.tracks[0].events[5], MidiController::Expression) &&
             isMidiControllerLsb(autoMidi.tracks[0].events[6], MidiController::Expression),
         "MIDI renderer should honor source expression quantization by default");
  expect(midiNote(autoMidi.tracks[9].events[1])->channel == 10, "MIDI renderer should skip channel 10 by default");
  expect(*midiPort(autoMidi.tracks[15].events[0]) == 1 && midiNote(autoMidi.tracks[15].events[1])->channel == 0,
         "MIDI renderer should move skipped-channel overflow to the next MIDI port");

  const MidiSequence forcedMidi =
      renderMidiSequence(performance, MidiExportOptions{
                                          .volumeResolution = MidiLevelResolution::SevenBit,
                                          .expressionResolution = MidiLevelResolution::SevenBit,
                                          .skipChannel10 = false,
                                          .bankSelectStyle = MidiBankSelectStyle::MsbAndLsb,
                                      });
  expect(midiBankSelect(forcedMidi.tracks[0].events[1])->bank == 130 &&
             midiBankSelect(forcedMidi.tracks[0].events[1])->writeLsb,
         "MIDI renderer should retain logical banks for combined MSB/LSB output when requested");
  expect(isMidiController(forcedMidi.tracks[0].events[3], MidiController::ChannelVolume),
         "MIDI renderer should allow forced 7-bit volume output");
  expect(isMidiController(forcedMidi.tracks[0].events[4], MidiController::Expression),
         "MIDI renderer should allow forced 7-bit expression output");
  expect(midiNote(forcedMidi.tracks[9].events[1])->channel == 9,
         "MIDI renderer should allow channel 10 when requested");
  expect(*midiPort(forcedMidi.tracks[15].events[0]) == 0 && midiNote(forcedMidi.tracks[15].events[1])->channel == 15,
         "MIDI renderer should use all 16 channels per port when channel 10 is allowed");
}

void performanceMidiRendererCanTerminatePreviousVoices() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .events =
              {
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 0},
                      .key = 60.0,
                      .durationTicks = 4,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 8, .sequence = 1},
                      .key = 62.0,
                      .durationTicks = 4,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 8, .sequence = 2},
                      .semitones = -1.0,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 10, .sequence = 3},
                      .key = 62.0,
                      .durationTicks = 4,
                      .extendsPrevious = true,
                  },
              },
      }},
  };

  const MidiSequence plain = renderMidiSequence(performance);
  expect(
      std::ranges::none_of(plain.tracks[0].events,
                           [](const MidiEvent& event) { return isMidiController(event, MidiController::AllSoundOff); }),
      "previous-voice termination should remain opt-in");

  MidiExportOptions options;
  options.terminatePreviousVoice = true;
  const MidiSequence terminated = renderMidiSequence(performance, options);
  const auto isSoundOff = [](const MidiEvent& event) { return isMidiController(event, MidiController::AllSoundOff); };
  const auto soundOff = std::ranges::find_if(terminated.tracks[0].events, isSoundOff);
  const auto attackBend = std::ranges::find_if(terminated.tracks[0].events, [](const MidiEvent& event) {
    return event.tick == 8 && isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend);
  });
  expect(
      std::ranges::count_if(terminated.tracks[0].events, isSoundOff) == 1 &&
          soundOff != terminated.tracks[0].events.end() && soundOff->tick == 8 &&
          attackBend != terminated.tracks[0].events.end() && soundOff->priority < attackBend->priority,
      "the renderer should terminate a previous voice before configuring a fresh attack, but not before an extension");
}

void performanceMidiRendererLowersStructuredScalarAutomationPoints() {
  const PerformanceEventHeader origin{
      .sourceCommand = {TrackId{0}, CommandId{7}},
      .track = TrackId{0},
      .tick = 0,
      .sequence = 0,
  };
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 4,
          .events =
              {
                  LevelPerformanceEvent{
                      .header =
                          PerformanceEventHeader{
                              .sourceCommand = {TrackId{0}, CommandId{7}},
                              .track = TrackId{0},
                              .tick = 0,
                              .sequence = 1,
                              .automation = PerformanceAutomationId{0},
                          },
                      .linearGain = 0.75,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 1, .sequence = 2},
                      .key = 60,
                      .durationTicks = 2,
                  },
                  LevelPerformanceEvent{
                      .header =
                          PerformanceEventHeader{
                              .sourceCommand = {TrackId{0}, CommandId{7}},
                              .track = TrackId{0},
                              .tick = 2,
                              .sequence = 3,
                              .automation = PerformanceAutomationId{0},
                          },
                      .linearGain = 0.5,
                  },
              },
          .automations = {PerformanceAutomation{
              .id = PerformanceAutomationId{0},
              .header = origin,
              .intent =
                  ScalarPerformanceAutomationIntent{
                      .target = PerformanceAutomationTarget::Level,
                      .targetValue = 0.5,
                      .durationTicks = 2,
                  },
          }},
      }},
  };

  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks.front().events;
  const auto firstVolume = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* volume = midiController(event, MidiController::ChannelVolume);
    return volume != nullptr && event.tick == 0;
  });
  const auto finalVolume = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* volume = midiController(event, MidiController::ChannelVolume);
    return volume != nullptr && event.tick == 2;
  });
  expect(firstVolume != events.end() && finalVolume != events.end(),
         "MIDI lowering should expand exact realized scalar-automation points");
  expect(std::ranges::any_of(events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event.payload);
                               return note != nullptr && event.tick == 1;
                             }),
         "scalar automation lowering should retain interleaved ordinary events");

  PerformanceSequence flatPerformance = performance;
  auto& flatTrack = flatPerformance.tracks.front();
  for (auto& event : flatTrack.events) {
    std::visit([](auto& typed) { typed.header.automation.reset(); }, event);
  }
  flatTrack.automations.clear();
  const MidiSequence flatMidi = renderMidiSequence(flatPerformance);
  expect(encodeMidiFile(midi) == encodeMidiFile(flatMidi),
         "structured scalar automation should lower identically to the same exact flat performance points");
}

void performanceMidiRendererSuppressesOnlyAutomationOwnedControllerDuplicates() {
  PerformanceTrack track{.id = TrackId{0}, .endTick = 4};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,    {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence,
                         nextNote, nextAutomation,           PanLaw::EqualPower};
  const auto level = out.fade(PerformanceAutomationTarget::Level, 0.5, 1);
  level.output(out).level(0.5);
  level.at(out, 1).level(0.5);
  const auto expression = out.fade(PerformanceAutomationTarget::Expression, 0.75, 2);
  expression.output(out).expression(0.75);
  expression.at(out, 2).expression(0.75);
  const auto pan = out.fade(PerformanceAutomationTarget::Pan, 0.0, 3);
  pan.output(out).pan(0.0);
  pan.at(out, 3).pan(0.0);

  for (const bool interveningWrites : {false, true}) {
    if (interveningWrites) {
      // These source commands follow the initial samples at tick zero. Each
      // automation must restore its value when its next sample arrives.
      out.level(0.25);
      out.expression(0.25);
      out.pan(1.0);
    }
    PerformanceSequence performance{.timebase = {.ppqn = 48}, .tracks = {track}};
    const auto midi = renderMidiSequence(performance);
    for (const auto controller : {MidiController::ChannelVolume, MidiController::Expression, MidiController::Pan}) {
      expect(std::ranges::count_if(midi.tracks[0].events,
                                   [=](const MidiEvent& event) { return isMidiController(event, controller); }) ==
                 (interveningWrites ? 3 : 1),
             "automation samples must compare against the channel value after any intervening source writes");
    }

    auto& flatTrack = performance.tracks.front();
    for (auto& event : flatTrack.events) {
      std::visit([](auto& typed) { typed.header.automation.reset(); }, event);
    }
    flatTrack.automations.clear();
    const auto flatMidi = renderMidiSequence(performance);
    if (interveningWrites) {
      expect(encodeMidiFile(midi) == encodeMidiFile(flatMidi),
             "interleaved automation and source writes must preserve the exact controller sequence");
    } else {
      for (const auto controller : {MidiController::ChannelVolume, MidiController::Expression, MidiController::Pan}) {
        expect(std::ranges::count_if(flatMidi.tracks[0].events,
                                     [=](const MidiEvent& event) { return isMidiController(event, controller); }) == 2,
               "automation deduplication should not remove repeated ordinary performance writes");
      }
    }
  }
}

void performanceMidiRendererSuppressesRedundantReverbSends() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .events =
              {
                  ReverbPerformanceEvent{.header = {.tick = 0}, .voiceMask = 1, .send = 0.0},
                  ReverbPerformanceEvent{.header = {.tick = 1}, .voiceMask = 1, .send = 0.0, .feedback = 0.5},
                  ReverbPerformanceEvent{.header = {.tick = 2}, .voiceMask = 1, .send = 0.5},
              }}},
  };

  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks[0].events;
  expect(std::ranges::count_if(
             events, [](const MidiEvent& event) { return isMidiController(event, MidiController::Reverb); }) == 2,
         "physical reverb changes should emit CC91 only when their portable wet-send value changes");
}

}  // namespace

void runValueMidiRendererTests() {
  performanceMidiRendererTrustsSourceNoteExtensions();
  performanceMidiRendererKeepsPhysicalLimitsAcrossPortamentoFragments();
  performanceMidiRendererKeepsPhysicalLimitsAcrossVoiceContinuations();
  performanceMidiRendererLimitsOnlyTheOwningVoice();
  performanceMidiRendererSelectsTuningRepresentation();
  performanceMidiRendererWritesTimeSignaturesToFirstTrack();
  performanceMidiRendererUsesGlobalExecutionOrderForTransposeAndMeter();
  performanceMidiRendererWritesPanGainResetWhenRequested();
  performanceMidiRendererKeepsPanGainOutOfExpression();
  performanceMidiRendererLowersDeclaredPanLaws();
  performanceMidiRendererRetainsPanLawDuringLfoSimulation();
  performanceMidiRendererHonorsMidiExportOptions();
  performanceMidiRendererCanTerminatePreviousVoices();
  performanceMidiRendererLowersStructuredScalarAutomationPoints();
  performanceMidiRendererSuppressesOnlyAutomationOwnedControllerDuplicates();
  performanceMidiRendererSuppressesRedundantReverbSends();
}

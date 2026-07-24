/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "SessionSnapshotBuilder.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"

#include <array>

namespace {

void midiExporterWritesStandardMidiFile() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .name = "Lead",
          .events =
              {
                  Tempo{.tick = 0, .microsecondsPerQuarter = 500000},
                  MidiPort{.tick = 0, .port = 2},
                  ProgramChange{.tick = 0, .channel = 0, .program = 5},
                  Volume{.tick = 0, .channel = 0, .value = 100},
                  NoteDuration{.tick = 0, .channel = 0, .key = 60, .velocity = 100, .duration = 24},
                  Pan{.tick = 12, .channel = 0, .value = 64},
                  EndOfTrack{.tick = 24},
              },
      }},
  };

  const std::vector<u8> expected{
      'M',  'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 'M',  'T',  'r',
      'k',  0x00, 0x00, 0x00, 0x2b, 0x00, 0xff, 0x03, 0x04, 'L',  'e',  'a',  'd',  0x00, 0xff, 0x21, 0x01,
      0x02, 0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20, 0x00, 0xc0, 0x05, 0x00, 0xb0, 0x07, 0x64, 0x00, 0x90,
      0x3c, 0x64, 0x0c, 0xb0, 0x0a, 0x40, 0x0c, 0x80, 0x3c, 0x40, 0x00, 0xff, 0x2f, 0x00,
  };

  const auto exported = encodeMidiFile(midiSequence);
  expect(exported == expected, "MIDI exporter should write expected SMF bytes");
}

void midiExporterKeeps14BitControllerPairsAdjacent() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  Volume14{.tick = 0, .channel = 0, .value = 0x1234},
                  Pan{.tick = 0, .channel = 0, .value = 64},
                  EndOfTrack{.tick = 0},
              },
      }},
  };

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x07, 0x24, 0x00, 0xb0, 0x27, 0x34, 0x00, 0xb0, 0x0a, 0x40,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should keep 14-bit volume MSB/LSB controllers adjacent before same-tick pan");
}

void midiExporterPreservesLegacyPortamentoTimeByteOrder() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  PortamentoTime14{.tick = 0, .channel = 0, .value = 0x01d3},
                  PortamentoControl{.tick = 0, .channel = 0, .key = 60},
                  EndOfTrack{.tick = 0},
              },
      }},
  };

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x25, 0x53, 0x00, 0xb0, 0x05, 0x03, 0x00, 0xb0, 0x54, 0x3c,
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should write fine then coarse portamento time before source-key control");
}

void midiExporterOrdersFineTuneBeforeSameTickProgramChange() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  FineTune{.tick = 0, .channel = 2, .cents = -46.875},
                  BankSelect{.tick = 0, .channel = 2, .bank = 0, .writeLsb = false},
                  ProgramChange{.tick = 0, .channel = 2, .program = 9},
                  EndOfTrack{.tick = 0},
              },
      }},
  };

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb2, 0x65, 0x00, 0x00, 0xb2, 0x64, 0x01, 0x00, 0xb2, 0x06, 0x22,
      0x00, 0xb2, 0x26, 0x00, 0x00, 0xb2, 0x00, 0x00, 0x00, 0xc2, 0x09,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should serialize fine tuning RPN before same-tick bank and program changes");
}

void midiExporterKeepsSameTickBankProgramPairsAdjacent() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  BankSelect{.tick = 0, .channel = 0, .bank = 0, .writeLsb = false},
                  ProgramChange{.tick = 0, .channel = 0, .program = 13},
                  BankSelect{.tick = 0, .channel = 0, .bank = 0x7f << 7, .writeLsb = false},
                  ProgramChange{.tick = 0, .channel = 0, .program = 0},
                  NoteDuration{.tick = 0, .channel = 0, .key = 60, .velocity = 100, .duration = 24},
                  EndOfTrack{.tick = 24},
              },
      }},
  };

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x00, 0x00, 0x00, 0xc0, 0x0d, 0x00, 0xb0, 0x00, 0x7f, 0x00, 0xc0, 0x00, 0x00, 0x90, 0x3c, 0x64,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should keep same-tick bank/program pairs adjacent before notes");
}

void midiExporterWritesTimeSignatureMetaEvent() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  TimeSignature{.tick = 0, .numerator = 3, .denominator = 4, .clocksPerMetronomeClick = 48},
                  EndOfTrack{.tick = 0},
              },
      }},
  };

  const std::vector<u8> expected{
      'M', 'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 'M',  'T',  'r',
      'k', 0x00, 0x00, 0x00, 0x0c, 0x00, 0xff, 0x58, 0x04, 0x03, 0x02, 0x30, 0x08, 0x00, 0xff, 0x2f, 0x00,
  };

  expect(encodeMidiFile(midiSequence) == expected, "MIDI exporter should write time-signature meta events");
}

void midiExporterOrdersGeneratedNoteOffBeforeSameTickNoteOn() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  NoteDuration{.tick = 10, .channel = 0, .key = 60, .velocity = 100, .duration = 10},
                  NoteDuration{.tick = 0, .channel = 0, .key = 60, .velocity = 100, .duration = 10},
                  EndOfTrack{.tick = 20},
              },
      }},
  };

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x0a, 0x80, 0x3c, 0x40, 0x00, 0x90, 0x3c, 0x64,
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should write generated note-off before same-tick note-on");
}

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
  expect(std::holds_alternative<MidiPort>(events[0]), "performance renderer should mark each track's MIDI port");
  const auto firstNote = std::get_if<NoteDuration>(&events[1]);
  const auto secondNote = std::get_if<NoteDuration>(&events[2]);
  expect(firstNote != nullptr && firstNote->tick == 0 && firstNote->key == 60 && firstNote->duration == 24,
         "performance renderer should trust source-selected note extensions");
  expect(secondNote != nullptr && secondNote->tick == 24 && secondNote->key == 61 && secondNote->duration == 6,
         "performance renderer should emit a new note when the source does not request an extension");
  const auto* timeSignature = std::get_if<TimeSignature>(&events[3]);
  expect(timeSignature != nullptr && timeSignature->tick == 30 && timeSignature->numerator == 3,
         "performance renderer should preserve source time signatures");
  expect(std::get<EndOfTrack>(events.back()).tick == 30, "performance renderer should preserve track end ticks");
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
  const auto* timeSignature = std::get_if<TimeSignature>(&firstTrackEvents[firstTrackEvents.size() - 2]);
  expect(timeSignature != nullptr && timeSignature->tick == 48 && timeSignature->numerator == 4,
         "performance renderer should write global time signatures to the first MIDI track");
  expect(std::get<EndOfTrack>(firstTrackEvents.back()).tick == 48,
         "first MIDI track end should cover global time signatures");
  expect(std::none_of(secondTrackEvents.begin(), secondTrackEvents.end(),
                      [](const MidiEvent& event) { return std::holds_alternative<TimeSignature>(event); }),
         "performance renderer should not duplicate time signatures on their source track");
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
                      .leftGain = 0.7071067811865476,
                      .rightGain = 0.7071067811865476,
                  },
              },
      }},
  };

  const MidiSequence midiSequence = renderMidiSequence(performance);
  const auto& events = midiSequence.tracks[0].events;
  expect(std::get<Pan>(events[1]).value == 0 && std::holds_alternative<Expression>(events[2]),
         "pan gain compensation should emit expression with the pan event");
  expect(std::get<Pan>(events[3]).value == 64 && std::get<Expression>(events[4]).value == 127,
         "full-gain compensated pan should reset expression to full scale");
}

void performanceMidiRendererCombinesExpressionWithPanGain() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 36,
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
                      .linearGain = 0.25,
                      .hasLinearGain = true,
                  },
              },
      }},
  };

  const auto expressionValues = [&](ModulationConversionPolicy policy) {
    const MidiSequence midi = renderMidiSequence(performance, MidiExportOptions{}, policy);
    std::vector<u8> values;
    for (const MidiEvent& event : midi.tracks[0].events) {
      if (const auto* expression = std::get_if<Expression>(&event)) {
        values.push_back(expression->value);
      }
    }
    return values;
  };

  const std::vector<u8> expected{64, 45, 64, 32};
  expect(expressionValues(ModulationConversionPolicy::SynthModulators) == expected,
         "synth-modulator MIDI lowering should multiply pan compensation by source expression");
  expect(expressionValues(ModulationConversionPolicy::SequenceEventSimulation) == expected,
         "sequence-event MIDI lowering should multiply pan compensation by source expression");

  PerformanceSequence precisePerformance = performance;
  std::get<ExpressionPerformanceEvent>(precisePerformance.tracks[0].events[0]).precisionHint =
      LevelPrecisionHint::FourteenBit;
  const MidiSequence preciseMidi = renderMidiSequence(precisePerformance);
  expect(std::count_if(preciseMidi.tracks[0].events.begin(), preciseMidi.tracks[0].events.end(),
                       [](const MidiEvent& event) { return std::holds_alternative<Expression14>(event); }) == 4,
         "pan compensation should preserve the source expression's precision");
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
                                                      .bank = 130,
                                                      .program = 5,
                                                  },
                                                  LevelPerformanceEvent{
                                                      .header = PerformanceEventHeader{.tick = 0},
                                                      .linearGain = 1.0,
                                                      .precisionHint = LevelPrecisionHint::FourteenBit,
                                                  },
                                                  ExpressionPerformanceEvent{
                                                      .header = PerformanceEventHeader{.tick = 0},
                                                      .linearGain = 1.0,
                                                      .precisionHint = LevelPrecisionHint::FourteenBit,
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
  expect(std::get<MidiPort>(autoMidi.tracks[0].events[0]).port == 0,
         "MIDI renderer should emit port zero for the first channel group");
  expect(std::get<BankSelect>(autoMidi.tracks[0].events[1]).writeLsb == false,
         "MIDI renderer should default to MSB-only bank select");
  expect(std::holds_alternative<Volume14>(autoMidi.tracks[0].events[3]),
         "MIDI renderer should honor 14-bit source volume hints by default");
  expect(std::holds_alternative<Expression14>(autoMidi.tracks[0].events[4]),
         "MIDI renderer should honor 14-bit source expression hints by default");
  expect(std::get<NoteDuration>(autoMidi.tracks[9].events[1]).channel == 10,
         "MIDI renderer should skip channel 10 by default");
  expect(std::get<MidiPort>(autoMidi.tracks[15].events[0]).port == 1 &&
             std::get<NoteDuration>(autoMidi.tracks[15].events[1]).channel == 0,
         "MIDI renderer should move skipped-channel overflow to the next MIDI port");

  const MidiSequence forcedMidi =
      renderMidiSequence(performance, MidiExportOptions{
                                          .volumeResolution = MidiLevelResolution::SevenBit,
                                          .expressionResolution = MidiLevelResolution::SevenBit,
                                          .skipChannel10 = false,
                                          .bankSelectStyle = MidiBankSelectStyle::MsbAndLsb,
                                      });
  expect(std::get<BankSelect>(forcedMidi.tracks[0].events[1]).writeLsb == true,
         "MIDI renderer should allow bank-select LSB output");
  expect(std::holds_alternative<Volume>(forcedMidi.tracks[0].events[3]),
         "MIDI renderer should allow forced 7-bit volume output");
  expect(std::holds_alternative<Expression>(forcedMidi.tracks[0].events[4]),
         "MIDI renderer should allow forced 7-bit expression output");
  expect(std::get<NoteDuration>(forcedMidi.tracks[9].events[1]).channel == 9,
         "MIDI renderer should allow channel 10 when requested");
  expect(std::get<MidiPort>(forcedMidi.tracks[15].events[0]).port == 0 &&
             std::get<NoteDuration>(forcedMidi.tracks[15].events[1]).channel == 15,
         "MIDI renderer should use all 16 channels per port when channel 10 is allowed");
}

void performanceMidiRendererLowersStructuredScalarAutomationPoints() {
  const PerformanceEventHeader origin{
      .sourceCommand = CommandId{7},
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
          .events = {NotePerformanceEvent{
              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 1, .sequence = 2},
              .key = 60,
              .durationTicks = 2,
          }},
          .automations = {PerformanceAutomation{
              .id = PerformanceAutomationId{0},
              .header = origin,
              .intent =
                  ScalarPerformanceAutomationIntent{
                      .target = PerformanceAutomationTarget::Level,
                      .targetValue = 0.5,
                      .durationTicks = 2,
                  },
              .points =
                  {
                      LevelPerformanceEvent{
                          .header =
                              PerformanceEventHeader{
                                  .sourceCommand = CommandId{7},
                                  .track = TrackId{0},
                                  .tick = 0,
                                  .sequence = 1,
                              },
                          .linearGain = 0.75,
                      },
                      LevelPerformanceEvent{
                          .header =
                              PerformanceEventHeader{
                                  .sourceCommand = CommandId{7},
                                  .track = TrackId{0},
                                  .tick = 2,
                                  .sequence = 3,
                              },
                          .linearGain = 0.5,
                      },
                  },
          }},
      }},
  };

  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks.front().events;
  const auto firstVolume = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* volume = std::get_if<Volume>(&event);
    return volume != nullptr && volume->tick == 0;
  });
  const auto finalVolume = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* volume = std::get_if<Volume>(&event);
    return volume != nullptr && volume->tick == 2;
  });
  expect(firstVolume != events.end() && finalVolume != events.end(),
         "MIDI lowering should expand exact realized scalar-automation points");
  expect(std::ranges::any_of(events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event);
                               return note != nullptr && note->tick == 1;
                             }),
         "scalar automation lowering should retain interleaved ordinary events");

  PerformanceSequence flatPerformance = performance;
  auto& flatTrack = flatPerformance.tracks.front();
  flatTrack.events.insert(flatTrack.events.end(), flatTrack.automations.front().points.begin(),
                          flatTrack.automations.front().points.end());
  flatTrack.automations.clear();
  const MidiSequence flatMidi = renderMidiSequence(flatPerformance);
  expect(encodeMidiFile(midi) == encodeMidiFile(flatMidi),
         "structured scalar automation should lower identically to the same exact flat performance points");
}

void performanceMidiRendererSuppressesOnlyAutomationOwnedControllerDuplicates() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .endTick = 4,
          .automations =
              {
                  PerformanceAutomation{
                      .id = PerformanceAutomationId{0},
                      .intent =
                          ScalarPerformanceAutomationIntent{
                              .target = PerformanceAutomationTarget::Level,
                          },
                      .points =
                          {
                              LevelPerformanceEvent{
                                  .header = PerformanceEventHeader{.tick = 0, .sequence = 0},
                                  .linearGain = 0.5,
                              },
                              LevelPerformanceEvent{
                                  .header = PerformanceEventHeader{.tick = 1, .sequence = 1},
                                  .linearGain = 0.5,
                              },
                          },
                  },
                  PerformanceAutomation{
                      .id = PerformanceAutomationId{1},
                      .intent =
                          ScalarPerformanceAutomationIntent{
                              .target = PerformanceAutomationTarget::Expression,
                          },
                      .points =
                          {
                              ExpressionPerformanceEvent{
                                  .header = PerformanceEventHeader{.tick = 0, .sequence = 2},
                                  .linearGain = 0.75,
                              },
                              ExpressionPerformanceEvent{
                                  .header = PerformanceEventHeader{.tick = 2, .sequence = 3},
                                  .linearGain = 0.75,
                              },
                          },
                  },
                  PerformanceAutomation{
                      .id = PerformanceAutomationId{2},
                      .intent =
                          ScalarPerformanceAutomationIntent{
                              .target = PerformanceAutomationTarget::Pan,
                          },
                      .points =
                          {
                              PanPerformanceEvent{
                                  .header = PerformanceEventHeader{.tick = 0, .sequence = 4},
                                  .stereoPosition = 0.0,
                              },
                              PanPerformanceEvent{
                                  .header = PerformanceEventHeader{.tick = 3, .sequence = 5},
                                  .stereoPosition = 0.0,
                              },
                          },
                  },
              },
      }},
  };

  const MidiSequence midi = renderMidiSequence(performance);
  expect(std::ranges::count_if(midi.tracks[0].events,
                               [](const MidiEvent& event) { return std::holds_alternative<Volume>(event); }) == 1 &&
             std::ranges::count_if(midi.tracks[0].events,
                                   [](const MidiEvent& event) { return std::holds_alternative<Expression>(event); }) ==
                 1 &&
             std::ranges::count_if(midi.tracks[0].events,
                                   [](const MidiEvent& event) { return std::holds_alternative<Pan>(event); }) == 1,
         "automation lowering should suppress repeated quantized volume, expression, and pan values");

  PerformanceSequence flatPerformance = performance;
  auto& flatTrack = flatPerformance.tracks.front();
  for (const auto& automation : flatTrack.automations) {
    flatTrack.events.insert(flatTrack.events.end(), automation.points.begin(), automation.points.end());
  }
  flatTrack.automations.clear();
  const MidiSequence flatMidi = renderMidiSequence(flatPerformance);
  expect(std::ranges::count_if(flatMidi.tracks[0].events,
                               [](const MidiEvent& event) { return std::holds_alternative<Volume>(event); }) == 2 &&
             std::ranges::count_if(flatMidi.tracks[0].events,
                                   [](const MidiEvent& event) { return std::holds_alternative<Expression>(event); }) ==
                 2 &&
             std::ranges::count_if(flatMidi.tracks[0].events,
                                   [](const MidiEvent& event) { return std::holds_alternative<Pan>(event); }) == 2,
         "automation deduplication should not remove repeated writes from ordinary performance events");
}

void performanceMidiRendererChoosesPitchTransitionRepresentationAtLowering() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{1}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
  out.pitchTransitionSettings(250.0);
  const PerformanceNoteId note = out.note(64, 1.0, 8);
  PitchSlideOptions slideOptions{
      .renderingHint = PitchTransitionRenderingHint::Portamento,
      .nativePortamento =
          NativePortamentoHint{
              .timeMilliseconds = 250.0,
              .emitTime = false,
          },
  };
  out.pitchSlide(note, 60, 64, 4, slideOptions);
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };

  const MidiSequence native = renderMidiSequence(performance);
  expect(std::ranges::any_of(native.tracks[0].events,
                             [](const MidiEvent& event) { return std::holds_alternative<PortamentoTime14>(event); }) &&
             std::ranges::any_of(
                 native.tracks[0].events,
                 [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) &&
             std::ranges::none_of(native.tracks[0].events,
                                  [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }),
         "preserve-format lowering should honor a format's native-portamento preference");

  const MidiSequence bent =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const auto noteEvent = std::ranges::find_if(
      bent.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  expect(
      noteEvent != bent.tracks[0].events.end() && std::get<NoteDuration>(*noteEvent).key == 64 &&
          std::ranges::any_of(bent.tracks[0].events,
                              [](const MidiEvent& event) { return std::holds_alternative<PitchBendRange>(event); }) &&
          std::ranges::any_of(bent.tracks[0].events,
                              [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }) &&
          std::ranges::none_of(bent.tracks[0].events,
                               [](const MidiEvent& event) {
                                 return std::holds_alternative<PortamentoTime14>(event) ||
                                        std::holds_alternative<PortamentoControl>(event);
                               }),
      "one parsed transition should lower to pitch bend without leaking native-portamento settings");
  expect(performance.tracks[0].automations.size() == 1 &&
             pitchTransitionIntent(performance.tracks[0].automations[0]) != nullptr,
         "MIDI lowering should leave the caller's target-neutral performance intact");
}

void performanceMidiRendererPreservesExactSamplesAndChainedPitchContinuity() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{3}, SourceAnnotationId{4}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId note = out.note(64, 1.0, 8);
  PitchSlideOptions firstOptions;
  firstOptions.renderingHint = PitchTransitionRenderingHint::PitchBend;
  firstOptions.interruptions.newAutomation = AutomationNewAutomationPolicy::Queue;
  const auto first = out.pitchSlide(note, 60, 62, 2, firstOptions);
  first.sample(out.at(1), 61.5);
  const auto second = out.at(1).pitchSlide(note, 0, 64, 2,
                                           PitchSlideOptions{
                                               .renderingHint = PitchTransitionRenderingHint::PitchBend,
                                           });
  second.sample(out.at(3), 63.5);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };
  const MidiSequence midi = renderMidiSequence(performance);

  std::vector<std::pair<u64, u16>> ranges;
  std::vector<std::pair<u64, s16>> bends;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* range = std::get_if<PitchBendRange>(&event)) {
      ranges.emplace_back(range->tick, range->cents);
    } else if (const auto* bend = std::get_if<PitchBend>(&event)) {
      bends.emplace_back(bend->tick, bend->value);
    }
  }

  expect(ranges == std::vector<std::pair<u64, u16>>{{0, 400}},
         "chained pitch bends should choose one range large enough for the complete note");
  expect(std::ranges::find(bends, std::pair<u64, s16>{1, -5120}) != bends.end() &&
             std::ranges::find(bends, std::pair<u64, s16>{3, -1024}) != bends.end(),
         "pitch-bend lowering should reproduce exact source samples rather than replacing them with a linear ramp");
  expect(std::ranges::none_of(bends, [](const auto& bend) { return bend.first == 2 && bend.second == 0; }),
         "queued pitch transitions should remain continuous at their shared boundary");
}

void performanceMidiRendererResetsInterruptedPitchBeforeTheNewNote() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{5}, SourceAnnotationId{6}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId firstNote = out.note(64, 1.0, 8);
  out.pitchSlide(firstNote, 60, 64, 6, PitchSlideOptions{.renderingHint = PitchTransitionRenderingHint::PitchBend});
  out.at(3).note(67, 1.0, 3);

  const MidiSequence midi = renderMidiSequence(PerformanceSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  });
  std::vector<std::pair<u64, s16>> bends;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* bend = std::get_if<PitchBend>(&event)) {
      bends.emplace_back(bend->tick, bend->value);
    }
  }
  expect(!bends.empty() && bends.back() == std::pair<u64, s16>{3, 0},
         "a new-note interruption should reset the channel bend at the interruption tick");
}

void performanceMidiLoweringCanContinueAnAbsoluteCurveAcrossNewNotes() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{7}, SourceAnnotationId{8}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId firstNote = out.note(64, 1.0, 4);
  PitchSlideOptions options{
      .interruptions =
          PerformanceAutomationInterruptPolicy{
              .newNote = AutomationNewNotePolicy::Continue,
              .noteEnd = AutomationNoteEndPolicy::Continue,
          },
      .renderingHint = PitchTransitionRenderingHint::PitchBend,
  };
  out.pitchSlide(firstNote, 60, 68, 8, options);
  out.at(4).note(67, 1.0, 4);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };
  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(performance, {});
  const auto continuedBend = std::ranges::find_if(lowered.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 4 && std::abs(bend->semitones - (-3.0)) < 0.000001;
  });
  expect(
      performance.tracks[0].automations[0].realization.endTick == 8 && continuedBend != lowered.tracks[0].events.end(),
      "a continuing transition should preserve its absolute curve and rebase it to the new note");
}

void performanceMidiRendererResolvesSourceInstrumentIdentityAtExport() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .events = {InstrumentPerformanceEvent{
              .header = PerformanceEventHeader{.tick = 0},
              .sourceInstrument = InstrumentIdentity{.domain = "probe.instrument", .key = 5},
          }},
      }},
  };
  const InstrumentSetAsset instrumentSet{
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 3, .program = 9},
          .identity = InstrumentIdentity{.domain = "probe.instrument", .key = 5},
      }},
  };
  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&instrumentSet};

  const MidiSequence midi =
      renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, instrumentSets);
  expect(std::get<BankSelect>(midi.tracks[0].events[1]).bank == (3 << 7) &&
             std::get<ProgramChange>(midi.tracks[0].events[2]).program == 9,
         "MSB-only MIDI lowering should resolve and pack logical collection instrument banks");

  const MidiSequence mmaMidi =
      renderMidiSequence(performance, MidiExportOptions{.bankSelectStyle = MidiBankSelectStyle::MsbAndLsb},
                         ModulationConversionPolicy::SynthModulators, instrumentSets);
  expect(std::get<BankSelect>(mmaMidi.tracks[0].events[1]).bank == 3,
         "MSB/LSB MIDI lowering should retain the logical collection instrument bank");
}

void performanceMidiRendererQuantizesPitchBendAndPortamento() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 24,
          .events =
              {
                  PitchBendRangePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .cents = 400,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .semitones = 1.0,
                  },
                  PortamentoTimePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 12},
                      .timeMilliseconds = 83.0,
                  },
              },
      }},
  };

  const MidiSequence midiSequence = renderMidiSequence(performance);
  const auto& events = midiSequence.tracks[0].events;
  expect(std::get<PitchBendRange>(events[1]).cents == 400,
         "MIDI renderer should emit the performance pitch-bend range");
  expect(std::get<PitchBend>(events[2]).value == 2048,
         "MIDI renderer should quantize semitone pitch bend through the active range");
  expect(std::get<PortamentoTime>(events[3]).value == 83,
         "MIDI renderer should quantize performance portamento milliseconds");
}

void performanceMidiRendererSkipsRedundantPitchBends() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 48,
          .events =
              {
                  PitchBendRangePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .cents = 200,
                  },
                  PitchBendRangePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 6},
                      .cents = 200,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .semitones = 0.0,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 12},
                      .semitones = 0.0,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 24},
                      .semitones = 1.0,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 36},
                      .semitones = 1.0,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 48},
                      .semitones = 0.0,
                  },
              },
      }},
  };

  const auto assertPitchBends = [](const MidiSequence& midiSequence, std::string_view label) {
    std::vector<std::pair<u64, u16>> pitchBendRanges;
    std::vector<std::pair<u64, s16>> pitchBends;
    for (const MidiEvent& event : midiSequence.tracks[0].events) {
      if (const auto* range = std::get_if<PitchBendRange>(&event)) {
        pitchBendRanges.emplace_back(range->tick, range->cents);
      } else if (const auto* pitchBend = std::get_if<PitchBend>(&event)) {
        pitchBends.emplace_back(pitchBend->tick, pitchBend->value);
      }
    }
    const std::vector<std::pair<u64, u16>> expectedPitchBendRanges{{0, 200}};
    const std::vector<std::pair<u64, s16>> expectedPitchBends{
        {0, 0},
        {24, 4096},
        {48, 0},
    };
    expect(pitchBendRanges == expectedPitchBendRanges,
           std::string(label) + " should skip repeated pitch bend range values");
    expect(pitchBends == expectedPitchBends, std::string(label) + " should skip repeated pitch bend values");
  };

  assertPitchBends(renderMidiSequence(performance), "synth-modulator MIDI lowering");
  assertPitchBends(
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation),
      "sequence-event MIDI lowering");
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
                  VibratoDelayPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .delayTicks = 2,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .amount = 1.0,
                      .frequencyHz = 12.5,
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
    const auto* pitchBend = std::get_if<PitchBend>(&event);
    if (pitchBend == nullptr) {
      continue;
    }
    if (pitchBend->tick < 2 && pitchBend->value != 0) {
      hasPreDelayNonzero = true;
    }
    pitchBends.emplace_back(pitchBend->tick, pitchBend->value);
  }

  expect(!hasPreDelayNonzero, "sequence-event vibrato simulation should stay silent before the delay expires");
  const std::vector<std::pair<u64, s16>> expectedPitchBends{
      {2, 0}, {3, 2048}, {4, 4096}, {5, 2048}, {6, 0}, {7, -2048}, {8, -4096},
  };
  expect(pitchBends == expectedPitchBends,
         "sequence-event vibrato simulation should emit a delayed triangle LFO bend shape");
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
                  VibratoDelayPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .delayTicks = 2,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .amount = 1.0,
                      .frequencyHz = 12.5,
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
    if (const auto* pitchBend = std::get_if<PitchBend>(&event)) {
      pitchBends.emplace_back(pitchBend->tick, pitchBend->value);
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
                  VibratoDelayPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .delayTicks = 2,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .amount = 1.0,
                      .frequencyHz = 12.5,
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
    if (const auto* pitchBend = std::get_if<PitchBend>(&event)) {
      pitchBends.emplace_back(pitchBend->tick, pitchBend->value);
    }
  }

  const std::vector<std::pair<u64, s16>> expectedPitchBends{
      {2, 0}, {3, 2048}, {4, 4096}, {5, 0}, {8, 2048}, {9, 4096}, {10, 2048},
  };
  expect(pitchBends == expectedPitchBends,
         "sequence-event vibrato simulation should restart the delay and phase for each new note");
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
                          TremoloDelayPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .delayTicks = 2,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::TremoloRate,
                              .amount = 1.0,
                              .frequencyHz = 25.0,
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
    if (const auto* expression = std::get_if<Expression>(&event)) {
      expressions.emplace_back(expression->tick, expression->value);
    }
  }
  const std::vector<std::pair<u64, u8>> expectedExpressions{
      {2, 127}, {3, 110}, {4, 90}, {5, 110}, {6, 127}, {7, 110}, {8, 90},
  };
  expect(expressions == expectedExpressions,
         "sequence-event tremolo should use a delayed LFO instead of static attenuation");
  expect(std::ranges::count_if(midiSequence.tracks[0].events,
                               [](const MidiEvent& event) { return std::holds_alternative<Tempo>(event); }) == 1 &&
             std::ranges::none_of(midiSequence.tracks[1].events,
                                  [](const MidiEvent& event) { return std::holds_alternative<Tempo>(event); }),
         "tempo output should remain anchored to its source track while driving simulation globally");
}

void exportRequestSequenceLoopsAffectMidiLowering() {
  expect(ExportRequest{}.sequence.sequenceLoops == 1,
         "the user-facing export request should default to one sequence loop");

  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder trackBuilder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x00, 0x00};
  addProbeCommand<ProbeNoteCommand>(trackBuilder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(trackBuilder, dialect, Address{3}, probeRange(3, jumpBytes.size()), jumpBytes);

  test::SessionSnapshotBuilder snapshotBuilder;
  snapshotBuilder.assets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{0},
              .format = "Probe",
              .name = "Looping Sequence",
          },
      .program =
          SequenceProgram{
              .dialect = dialect.id,
              .timebase = dialect.timebase,
              .tracks = {track},
          },
  });
  snapshotBuilder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Looping",
      .sequence = AssetId{0},
  });
  const SessionSnapshot project = snapshotBuilder.finish();

  SourceStore sources;
  SequenceDialectRegistry dialects;
  dialects.add(dialect);

  const auto artifacts = exportCollection(project, sources, CollectionId{0},
                                          ExportRequest{
                                              .kinds = {ExportKind::Midi},
                                              .sequence =
                                                  {
                                                      .loopPolicy = LoopPolicy::PlayOnce,
                                                      .sequenceLoops = 2,
                                                  },
                                          },
                                          dialects);

  expect(artifacts.size() == 1 && artifacts[0].diagnostics.empty(),
         "MIDI export with configured sequence loops should produce one clean artifact");
  const auto noteOnCount = std::ranges::count(artifacts[0].bytes, static_cast<u8>(0x90));
  expect(noteOnCount == 3, "ExportRequest sequenceLoops should replay the loop before MIDI rendering");
}

void standaloneSequenceExportDoesNotRequireACollection() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder trackBuilder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(trackBuilder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeEndCommand>(trackBuilder, dialect, Address{3}, probeRange(3, endBytes.size()), endBytes);

  test::SessionSnapshotBuilder snapshotBuilder;
  snapshotBuilder.assets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{7},
              .format = "Probe",
              .name = "Loose/Sequence",
          },
      .program =
          SequenceProgram{
              .dialect = dialect.id,
              .timebase = dialect.timebase,
              .tracks = {track},
          },
  });
  const SessionSnapshot snapshot = snapshotBuilder.finish();
  expect(snapshot.collections().empty(), "standalone MIDI fixture should not contain a collection");

  SequenceDialectRegistry dialects;
  dialects.add(dialect);
  const SourceStore sources;
  const Artifact artifact = exportSequenceMidi(snapshot, sources, AssetId{7}, SequenceExportRequest{}, dialects);

  expect(artifact.filename == "Loose_Sequence.mid",
         "standalone sequence export should derive a safe filename from sequence metadata");
  expect(artifact.mediaType == "audio/midi", "standalone sequence export should identify Standard MIDI data");
  expect(artifact.diagnostics.empty(), "standalone sequence export should not require collection diagnostics");
  expect(artifact.bytes.size() > 14 && std::string(artifact.bytes.begin(), artifact.bytes.begin() + 4) == "MThd",
         "standalone sequence export should produce a Standard MIDI file");
}

PerformanceSequence observedModulationPerformance() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              PerformanceTrack{
                  .id = TrackId{0},
                  .sourceTrackNumber = 0,
                  .events =
                      {
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::VibratoDepth,
                              .amount = 0.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 12},
                              .target = ModulationPerformanceTarget::VibratoDepth,
                              .amount = 82.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 12},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .amount = 17.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 24},
                              .target = ModulationPerformanceTarget::TremoloDepth,
                              .amount = 40.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 24},
                              .target = ModulationPerformanceTarget::TremoloRate,
                              .amount = 5.0 / 127.0,
                          },
                      },
              },
              PerformanceTrack{
                  .id = TrackId{1},
                  .sourceTrackNumber = 1,
                  .events =
                      {
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .amount = 29.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::TremoloRate,
                              .amount = 9.0 / 127.0,
                          },
                      },
              },
          },
  };
  return performance;
}

void modulationAnalysisReportsObservedPerformanceRanges() {
  const auto usage = analyzePerformanceModulationUsage(observedModulationPerformance());
  expect(hasMidiModulationUsage(usage), "performance modulation analysis should report observed driver modulation");
  expect(usage.tracks.size() == 2, "performance modulation analysis should preserve track-level results");
  expect(usage.vibratoDepth.observed && usage.vibratoDepth.min == 0 && usage.vibratoDepth.max == 82,
         "performance modulation analysis should report global vibrato depth range");
  expect(usage.vibratoRate.observed && usage.vibratoRate.min == 17 && usage.vibratoRate.max == 29,
         "performance modulation analysis should report global vibrato rate range");
  expect(usage.tremoloDepth.observed && usage.tremoloDepth.min == 40 && usage.tremoloDepth.max == 40,
         "performance modulation analysis should report global tremolo depth range");
  expect(usage.tremoloRate.observed && usage.tremoloRate.min == 5 && usage.tremoloRate.max == 9,
         "performance modulation analysis should report global tremolo rate range");
  expect(usage.tracks[0].trackIndex == 0 && usage.tracks[0].vibratoDepth.max == 82 &&
             usage.tracks[0].vibratoRate.max == 17,
         "performance modulation analysis should keep first track modulation ranges separate");
  expect(usage.tracks[1].trackIndex == 1 && !usage.tracks[1].vibratoDepth.observed &&
             usage.tracks[1].vibratoRate.max == 29,
         "performance modulation analysis should keep second track modulation ranges separate");
}

void observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators() {
  MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              MidiTrack{
                  .name = "Lead",
                  .events =
                      {
                          VibratoDepth{.tick = 0, .channel = 0, .value = 0},
                          VibratoDepth{.tick = 6, .channel = 0, .value = 41},
                          VibratoDepth{.tick = 12, .channel = 0, .value = 82},
                          VibratoFrequency{.tick = 18, .channel = 0, .value = 17},
                          TremoloDepth{.tick = 24, .channel = 0, .value = 40},
                          TremoloFrequency{.tick = 30, .channel = 0, .value = 5},
                          TremoloFrequency{.tick = 36, .channel = 0, .value = 9},
                      },
              },
              MidiTrack{
                  .name = "Pad",
                  .events =
                      {
                          VibratoFrequency{.tick = 0, .channel = 1, .value = 29},
                      },
              },
          },
  };

  const auto usage = analyzePerformanceModulationUsage(observedModulationPerformance());
  expect(scaledMidiModulationControllerValue(41, &usage.vibratoDepth, ModulationScalingPolicy::FullFormatRange) == 41,
         "full-range modulation scaling should leave MIDI controller values unchanged");

  applyMidiModulationScaling(midiSequence, usage, ModulationScalingPolicy::ObservedSequenceRange);

  const auto& leadEvents = midiSequence.tracks[0].events;
  expect(std::get<VibratoDepth>(leadEvents[0]).value == 0,
         "observed modulation scaling should preserve zero controller values");
  expect(std::get<VibratoDepth>(leadEvents[1]).value == 64,
         "observed modulation scaling should expand intermediate controller values");
  expect(std::get<VibratoDepth>(leadEvents[2]).value == 127,
         "observed modulation scaling should expand the observed maximum to full MIDI controller range");
  expect(std::get<VibratoFrequency>(leadEvents[3]).value == 74,
         "observed modulation scaling should use global rate range across tracks");
  expect(std::get<TremoloDepth>(leadEvents[4]).value == 127,
         "observed modulation scaling should expand tremolo depth controllers");
  expect(
      std::get<TremoloFrequency>(leadEvents[5]).value == 71 && std::get<TremoloFrequency>(leadEvents[6]).value == 127,
      "observed modulation scaling should expand tremolo rate controllers");

  const SynthModulator defaultTremoloRate{
      .destination = SynthDestination::TremoloRate,
      .amount = 180,
  };
  const SynthModulator explicitVibratoDepth{
      .source = SynthSource::NoteOnVelocity,
      .destination = SynthDestination::VibratoDepth,
      .amount = 300,
  };
  expect(scaledSynthModulatorAmount(defaultTremoloRate, &usage, ModulationScalingPolicy::ObservedSequenceRange) == 13,
         "observed modulation scaling should reduce default synth modulator amounts");
  expect(
      scaledSynthModulatorAmount(explicitVibratoDepth, &usage, ModulationScalingPolicy::ObservedSequenceRange) == 300,
      "observed modulation scaling should not change explicit-source synth modulator amounts");
}

void observedModulationScalingUsesPreciseNormalizedAmounts() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .events =
              {
                  ModulationPerformanceEvent{
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .amount = 0.006862745098,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 12},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .amount = 0.015686274510,
                  },
              },
      }},
  };
  MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  VibratoDepth{.tick = 0, .channel = 0, .value = 1, .normalizedAmount = 0.006862745098},
                  VibratoDepth{.tick = 12, .channel = 0, .value = 2, .normalizedAmount = 0.015686274510},
              },
      }},
  };

  const auto usage = analyzePerformanceModulationUsage(performance);
  applyMidiModulationScaling(midiSequence, usage, ModulationScalingPolicy::ObservedSequenceRange);

  const auto& events = midiSequence.tracks[0].events;
  expect(std::get<VibratoDepth>(events[0]).value == 56,
         "observed modulation scaling should use precise source amounts instead of rescaling rounded 7-bit values");
  expect(std::get<VibratoDepth>(events[1]).value == 127,
         "observed modulation scaling should expand the precise observed maximum to full controller range");
}

}  // namespace

void runValueMidiTests() {
  midiExporterWritesStandardMidiFile();
  midiExporterKeeps14BitControllerPairsAdjacent();
  midiExporterPreservesLegacyPortamentoTimeByteOrder();
  midiExporterOrdersFineTuneBeforeSameTickProgramChange();
  midiExporterKeepsSameTickBankProgramPairsAdjacent();
  midiExporterWritesTimeSignatureMetaEvent();
  midiExporterOrdersGeneratedNoteOffBeforeSameTickNoteOn();
  performanceMidiRendererTrustsSourceNoteExtensions();
  performanceMidiRendererWritesTimeSignaturesToFirstTrack();
  performanceMidiRendererWritesPanGainResetWhenRequested();
  performanceMidiRendererCombinesExpressionWithPanGain();
  performanceMidiRendererHonorsMidiExportOptions();
  performanceMidiRendererLowersStructuredScalarAutomationPoints();
  performanceMidiRendererSuppressesOnlyAutomationOwnedControllerDuplicates();
  performanceMidiRendererChoosesPitchTransitionRepresentationAtLowering();
  performanceMidiRendererPreservesExactSamplesAndChainedPitchContinuity();
  performanceMidiRendererResetsInterruptedPitchBeforeTheNewNote();
  performanceMidiLoweringCanContinueAnAbsoluteCurveAcrossNewNotes();
  performanceMidiRendererResolvesSourceInstrumentIdentityAtExport();
  performanceMidiRendererQuantizesPitchBendAndPortamento();
  performanceMidiRendererSkipsRedundantPitchBends();
  performanceMidiRendererSimulatesDelayedVibratoAsPitchBendShape();
  performanceMidiRendererDoesNotDoubleDelayVibrato();
  performanceMidiRendererRestartsSimulatedVibratoDelayForNewNotes();
  performanceMidiRendererSimulatesTremoloUsingGlobalTempo();
  exportRequestSequenceLoopsAffectMidiLowering();
  standaloneSequenceExportDoesNotRequireACollection();
  modulationAnalysisReportsObservedPerformanceRanges();
  observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators();
  observedModulationScalingUsesPreciseNormalizedAmounts();
}

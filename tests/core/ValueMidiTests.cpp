/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "SessionSnapshotBuilder.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"
#include "value/sequence/TempoRelativeModulation.h"

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

void midiExporterKeepsZeroDurationNotePairedAtSameTick() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .events =
              {
                  NoteDuration{.tick = 10, .channel = 0, .key = 60, .velocity = 90, .duration = 0},
                  NoteDuration{.tick = 10, .channel = 0, .key = 60, .velocity = 100, .duration = 5},
                  EndOfTrack{.tick = 15},
              },
      }},
  };

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x0a, 0x90, 0x3c, 0x5a,  // zero-duration attack
      0x00, 0x80, 0x3c, 0x40,  // its same-tick release
      0x00, 0x90, 0x3c, 0x64,  // following attack of the same key
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "zero-duration notes should close before a following same-key attack");
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
                      .law = PanLaw::EqualPower,
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
  expect(std::ranges::none_of(midi.tracks[0].events,
                              [](const MidiEvent& event) { return std::holds_alternative<Expression>(event); }),
         "equal-power positional pan should not add loudness compensation");
  const auto constantSumExpression = std::ranges::find_if(
      midi.tracks[1].events, [](const MidiEvent& event) { return std::holds_alternative<Expression>(event); });
  expect(
      constantSumExpression != midi.tracks[1].events.end() && std::get<Expression>(*constantSumExpression).value == 107,
      "constant-sum center pan should retain its lower combined gain when lowered to MIDI equal-power pan");
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
          .events =
              {
                  LevelPerformanceEvent{
                      .header =
                          PerformanceEventHeader{
                              .sourceCommand = CommandId{7},
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
                              .sourceCommand = CommandId{7},
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
  for (auto& event : flatTrack.events) {
    std::visit([](auto& typed) { typed.header.automation.reset(); }, event);
  }
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
          .events =
              {
                  LevelPerformanceEvent{
                      .header =
                          PerformanceEventHeader{.tick = 0, .sequence = 0, .automation = PerformanceAutomationId{0}},
                      .linearGain = 0.5,
                  },
                  LevelPerformanceEvent{
                      .header =
                          PerformanceEventHeader{.tick = 1, .sequence = 1, .automation = PerformanceAutomationId{0}},
                      .linearGain = 0.5,
                  },
                  ExpressionPerformanceEvent{
                      .header =
                          PerformanceEventHeader{.tick = 0, .sequence = 2, .automation = PerformanceAutomationId{1}},
                      .linearGain = 0.75,
                  },
                  ExpressionPerformanceEvent{
                      .header =
                          PerformanceEventHeader{.tick = 2, .sequence = 3, .automation = PerformanceAutomationId{1}},
                      .linearGain = 0.75,
                  },
                  PanPerformanceEvent{
                      .header =
                          PerformanceEventHeader{.tick = 0, .sequence = 4, .automation = PerformanceAutomationId{2}},
                      .stereoPosition = 0.0,
                      .law = PanLaw::EqualPower,
                  },
                  PanPerformanceEvent{
                      .header =
                          PerformanceEventHeader{.tick = 3, .sequence = 5, .automation = PerformanceAutomationId{2}},
                      .stereoPosition = 0.0,
                      .law = PanLaw::EqualPower,
                  },
              },
          .automations =
              {
                  PerformanceAutomation{
                      .id = PerformanceAutomationId{0},
                      .intent =
                          ScalarPerformanceAutomationIntent{
                              .target = PerformanceAutomationTarget::Level,
                          },
                  },
                  PerformanceAutomation{
                      .id = PerformanceAutomationId{1},
                      .intent =
                          ScalarPerformanceAutomationIntent{
                              .target = PerformanceAutomationTarget::Expression,
                          },
                  },
                  PerformanceAutomation{
                      .id = PerformanceAutomationId{2},
                      .intent =
                          ScalarPerformanceAutomationIntent{
                              .target = PerformanceAutomationTarget::Pan,
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
  for (auto& event : flatTrack.events) {
    std::visit([](auto& typed) { typed.header.automation.reset(); }, event);
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
  out.tempo(1'000'000);
  const PerformanceNoteId note = out.note(NotePerformanceEvent{
      .key = 64,
      .linearVelocity = 0.375,
      .durationTicks = 8,
      .lane = PerformanceLaneId{3},
  });
  out.pitchSlide(note, 60, 64, 4, PerformanceLaneId{3});
  out.at(2).tempo(500'000);

  PerformanceTrack rateTrack{
      .id = TrackId{1},
      .sourceTrackNumber = 1,
      .endTick = 8,
  };
  u64 rateSequence = 0;
  u32 rateNote = 0;
  u32 rateAutomation = 0;
  PerformanceEmitter rateOut{rateTrack, CommandId{3}, SourceAnnotationId{4}, 0, rateSequence, rateNote, rateAutomation};
  const PerformanceNoteId rateNoteId = rateOut.note(64, 1.0, 8);
  rateOut.pitchSlide(rateNoteId, 60, 64, PitchSlideTiming::fixedRate(4, 2.0));

  PerformanceTrack fixedTrack{
      .id = TrackId{2},
      .sourceTrackNumber = 2,
      .endTick = 8,
  };
  u64 fixedSequence = 0;
  u32 fixedNote = 0;
  u32 fixedAutomation = 0;
  PerformanceEmitter fixedOut{fixedTrack,    CommandId{5}, SourceAnnotationId{6}, 0,
                              fixedSequence, fixedNote,    fixedAutomation};
  const PerformanceNoteId fixedNoteId = fixedOut.note(64, 1.0, 8);
  fixedOut.pitchSlide(fixedNoteId, 60, 64, PitchSlideTiming::fixedDuration(4, 125.0));

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track, rateTrack, fixedTrack},
  };

  const MidiSequence native = renderMidiSequence(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  const auto portamentoTime = std::ranges::find_if(
      native.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<PortamentoTime14>(event); });
  expect(portamentoTime != native.tracks[0].events.end() && std::get<PortamentoTime14>(*portamentoTime).value == 63 &&
             std::ranges::any_of(
                 native.tracks[0].events,
                 [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) &&
             std::ranges::none_of(native.tracks[0].events,
                                  [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }),
         "portamento lowering should derive physical duration from sequence ticks and tempo");
  const auto rateTime = std::ranges::find_if(
      native.tracks[1].events, [](const MidiEvent& event) { return std::holds_alternative<PortamentoTime14>(event); });
  expect(rateTime != native.tracks[1].events.end() && std::get<PortamentoTime14>(*rateTime).value == 2000,
         "fixed-rate timing should derive portamento duration from pitch distance independently of tempo");
  const auto fixedTime = std::ranges::find_if(
      native.tracks[2].events, [](const MidiEvent& event) { return std::holds_alternative<PortamentoTime14>(event); });
  expect(fixedTime != native.tracks[2].events.end() && std::get<PortamentoTime14>(*fixedTime).value == 125,
         "fixed-duration timing should preserve source physical time independently of tempo");

  const MidiSequence bent =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const PerformanceSequence bendLowering = lowerMidiPerformanceAutomation(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const auto sourceNote = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  const auto loweredNote = std::ranges::find_if(bendLowering.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  const auto notesMatch = [](const NotePerformanceEvent& lhs, const NotePerformanceEvent& rhs) {
    return lhs.header.sourceCommand == rhs.header.sourceCommand &&
           lhs.header.sourceAnnotation == rhs.header.sourceAnnotation && lhs.header.track == rhs.header.track &&
           lhs.header.tick == rhs.header.tick && lhs.header.sequence == rhs.header.sequence &&
           lhs.header.automation == rhs.header.automation && lhs.key == rhs.key &&
           lhs.linearVelocity == rhs.linearVelocity && lhs.durationTicks == rhs.durationTicks &&
           lhs.extendsPrevious == rhs.extendsPrevious && lhs.restartsLfoPhase == rhs.restartsLfoPhase &&
           lhs.restartsVibratoLfoPhase == rhs.restartsVibratoLfoPhase &&
           lhs.restartsTremoloLfoPhase == rhs.restartsTremoloLfoPhase &&
           lhs.note == rhs.note && lhs.lane == rhs.lane;
  };
  expect(sourceNote != performance.tracks[0].events.end() && loweredNote != bendLowering.tracks[0].events.end() &&
             notesMatch(std::get<NotePerformanceEvent>(*sourceNote), std::get<NotePerformanceEvent>(*loweredNote)),
         "pitch-bend lowering should preserve the source note event verbatim");
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

void performanceMidiRendererAllowsMixedPitchTransitionRendering() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{1}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 64, 4).continueFrom(first).preferPortamento();
  const PerformanceNoteId third = out.at(8).note(67, 1.0, 4);
  out.at(8).pitchSlide(third, 64, 67, 4).continueFrom(second).preferPitchBend();

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };

  const auto countPortamento = [](const MidiSequence& midi) {
    return std::ranges::count_if(
        midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); });
  };
  const auto countPitchBends = [](const MidiSequence& midi) {
    return std::ranges::count_if(midi.tracks[0].events,
                                 [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); });
  };
  const auto noteDuration = [](const MidiSequence& midi, u64 tick) -> std::optional<u32> {
    const auto found = std::ranges::find_if(midi.tracks[0].events, [tick](const MidiEvent& event) {
      const auto* note = std::get_if<NoteDuration>(&event);
      return note != nullptr && note->tick == tick;
    });
    return found == midi.tracks[0].events.end() ? std::nullopt : std::optional{std::get<NoteDuration>(*found).duration};
  };

  const MidiSequence preserved = renderMidiSequence(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  expect(countPortamento(preserved) == 1 && countPitchBends(preserved) != 0,
         "PreserveFormat should allow portamento and pitch bend transitions in one track");
  expect(noteDuration(preserved, 0) == 5 && noteDuration(preserved, 4) == 8 && !noteDuration(preserved, 8),
         "pitch-bend continuation should retain the voice started by native portamento");

  const MidiSequence allPortamento =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(countPortamento(allPortamento) == 2 && countPitchBends(allPortamento) == 0 &&
             noteDuration(allPortamento, 0) == 5 && noteDuration(allPortamento, 4) == 5 &&
             noteDuration(allPortamento, 8) == 4,
         "an explicit portamento request should override every transition preference");

  const MidiSequence allPitchBend =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(countPortamento(allPitchBend) == 0 && countPitchBends(allPitchBend) != 0 &&
             noteDuration(allPitchBend, 0) == 12 && !noteDuration(allPitchBend, 4) && !noteDuration(allPitchBend, 8),
         "an explicit pitch-bend request should preserve one attack through linked transitions");
}

void performanceMidiRendererRetainsHeldVoiceAcrossChainedPitchBends() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{1}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 64, 4).continueFrom(first);
  const PerformanceNoteId third = out.at(8).note(67, 1.0, 4);
  out.at(8).pitchSlide(third, 64, 67, 4).continueFrom(second);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };
  const MidiExportOptions bendOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend};
  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(performance, bendOptions);
  const MidiSequence midi = renderMidiSequence(performance, bendOptions);

  std::vector<NoteDuration> notes;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* note = std::get_if<NoteDuration>(&event)) {
      notes.push_back(*note);
    }
  }
  expect(notes.size() == 1 && notes[0].tick == 0 && notes[0].key == 60 && notes[0].duration == 12,
         "linked pitch bends should sustain one MIDI note instead of retriggering each destination");

  std::vector<std::pair<u64, u16>> ranges;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* range = std::get_if<PitchBendRange>(&event)) {
      ranges.emplace_back(range->tick, range->cents);
    }
  }
  expect(ranges == std::vector<std::pair<u64, u16>>{{4, 700}},
         "one linked MIDI voice should keep a stable range large enough for its entire pitch path");
  const auto hasMidiBend = [&](u64 tick, s16 value) {
    return std::ranges::any_of(midi.tracks[0].events, [&](const MidiEvent& event) {
      const auto* bend = std::get_if<PitchBend>(&event);
      return bend != nullptr && bend->tick == tick && bend->value == value;
    });
  };
  expect(hasMidiBend(8, 4681) && hasMidiBend(12, 8191),
         "chained transitions should change bend values without retuning through mid-voice RPN updates");

  const auto chainedStart = std::ranges::find_if(lowered.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 8 && std::abs(bend->semitones - 4.0) < 0.000001;
  });
  expect(chainedStart != lowered.tracks[0].events.end(),
         "a chained bend should remain relative to the MIDI key that began the held voice");
}

void performanceMidiRendererRetriggersUnlinkedPitchBendDestinations() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{3}, SourceAnnotationId{4}, 0, nextSequence, nextNote, nextAutomation};
  out.note(60, 1.0, 4);
  const PerformanceNoteId destination = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(destination, 60, 64, 4);

  const MidiSequence midi = renderMidiSequence(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .tracks = {track},
      },
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const auto noteCount = std::ranges::count_if(
      midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  expect(noteCount == 2, "a boundary pitch slide without continueFrom should retain the destination note's attack");
}

void performanceMidiRendererStartsANewVoiceAfterPitchBendContinuationWhenNativePortamentoTakesOver() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{5}, SourceAnnotationId{6}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 64, 4).continueFrom(first).preferPitchBend();
  const PerformanceNoteId third = out.at(8).note(67, 1.0, 4);
  out.at(8).pitchSlide(third, 64, 67, 4).continueFrom(second).preferPortamento();

  const MidiSequence midi = renderMidiSequence(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .tracks = {track},
      },
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  std::vector<NoteDuration> notes;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* note = std::get_if<NoteDuration>(&event)) {
      notes.push_back(*note);
    }
  }
  expect(notes.size() == 2 && notes[0].tick == 0 && notes[0].key == 60 && notes[0].duration == 9 &&
             notes[1].tick == 8 && notes[1].key == 67,
         "native portamento after a held bend should overlap the actual held voice and then start its destination");
}

void performanceMidiRendererResetsHeldPitchBeforeNativePortamentoTakesOver() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{7}, SourceAnnotationId{8}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 8);
  out.at(4).pitchSlide(second, 60, 64, 4).continueFrom(first).preferPitchBend();
  out.at(8).pitchSlide(second, 64, 67, 4).preferPortamento();

  const MidiSequence midi = renderMidiSequence(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .tracks = {track},
      },
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  std::optional<s16> finalBendAtTakeover;
  for (const auto& event : midi.tracks[0].events) {
    const auto* bend = std::get_if<PitchBend>(&event);
    if (bend != nullptr && bend->tick == 8) {
      finalBendAtTakeover = bend->value;
    }
  }
  expect(finalBendAtTakeover == 0,
         "native portamento replacing a held pitch bend should receive an untransposed destination note");
}

void performanceMidiRendererCombinesPitchSlidesWithSimulatedVibrato() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{1}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
  out.tempo(1'000'000);
  out.modulation(ModulationPerformanceEvent{
      .target = ModulationPerformanceTarget::VibratoRate,
      .amount = 1.0,
      .frequencyHz = 12.5,
  });
  out.modulation(ModulationPerformanceEvent{
      .target = ModulationPerformanceTarget::VibratoDepth,
      .amount = 0.5,
      .pitchDepthSemitones = 1.0,
  });
  const PerformanceNoteId note = out.note(64, 1.0, 8);
  out.pitchSlide(note, 60, 64, 4).preferPitchBend();

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {track},
  };
  const MidiSequence synthModulators = renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators);
  const MidiSequence simulated =
      renderMidiSequence(performance, {}, ModulationConversionPolicy::SequenceEventSimulation);

  const auto lastPitchBendAt = [](const MidiSequence& midi, u64 tick) -> std::optional<s16> {
    std::optional<s16> result;
    for (const auto& event : midi.tracks[0].events) {
      const auto* bend = std::get_if<PitchBend>(&event);
      if (bend != nullptr && bend->tick == tick) {
        result = bend->value;
      }
    }
    return result;
  };

  expect(lastPitchBendAt(synthModulators, 2) == -4096,
         "synth-modulator export should retain the slide's unmodulated pitch bend");
  expect(lastPitchBendAt(simulated, 2) == -3072,
         "sequence-event export should add simulated vibrato around the active pitch slide");
}

void performanceMidiRendererUsesOnlyFrozenVibratoOffsetForPitchRange() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 3,
          .events =
              {
                  PitchBendRangePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .cents = 200,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .frequencyHz = 0.0,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .pitchDepthSemitones = 0.75,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 1},
                      .semitones = 2.0,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 2},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .frequencyHz = 1.0,
                  },
              },
      }},
  };

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  std::vector<std::pair<u64, u16>> pitchBendRanges;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* range = std::get_if<PitchBendRange>(&event)) {
      pitchBendRanges.emplace_back(range->tick, range->cents);
    }
  }

  const std::vector<std::pair<u64, u16>> expectedPitchBendRanges{{0, 200}, {2, 300}};
  expect(pitchBendRanges == expectedPitchBendRanges,
         "frozen vibrato should reserve only its current offset and restore full-depth headroom when resumed");
}

void performanceMidiRendererUsesWholeSemitonePitchBendRanges() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 1,
          .events =
              {
                  PitchBendRangePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .cents = 235,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .semitones = 1.6,
                  },
              },
      }},
  };

  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks[0].events;
  expect(std::get<PitchBendRange>(events[1]).cents == 300,
         "MIDI renderer should round pitch-bend ranges upward to whole semitones");
  expect(std::get<PitchBend>(events[2]).value == 4369,
         "MIDI renderer should quantize pitch bends using the emitted whole-semitone range");
}

void performanceMidiRendererDoesNotRestartVibratoAtAHeldPitchSlideBoundary() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{9}, SourceAnnotationId{10}, 0, nextSequence, nextNote, nextAutomation};
  out.tempo(1'000'000);
  out.modulation(ModulationPerformanceEvent{
      .target = ModulationPerformanceTarget::VibratoRate,
      .amount = 1.0,
      .frequencyHz = 25.0,
  });
  out.modulation(ModulationPerformanceEvent{
      .target = ModulationPerformanceTarget::VibratoDepth,
      .amount = 0.5,
      .pitchDepthSemitones = 1.0,
  });
  out.vibratoDelay(6, 0);
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 64, 4).continueFrom(first);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {track},
  };
  const MidiExportOptions bendOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend};
  const MidiSequence plain = renderMidiSequence(performance, bendOptions, ModulationConversionPolicy::SynthModulators);
  const MidiSequence simulated =
      renderMidiSequence(performance, bendOptions, ModulationConversionPolicy::SequenceEventSimulation);
  const auto lastPitchBendAt = [](const MidiSequence& midi, u64 tick) -> std::optional<s16> {
    std::optional<s16> result;
    for (const auto& event : midi.tracks[0].events) {
      if (const auto* bend = std::get_if<PitchBend>(&event); bend != nullptr && bend->tick == tick) {
        result = bend->value;
      }
    }
    return result;
  };

  expect(lastPitchBendAt(plain, 7) != lastPitchBendAt(simulated, 7),
         "a suppressed destination attack should not restart the held voice's vibrato delay");
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
  auto first = out.pitchSlide(note, 60, 62, 2);
  first.sample(out.at(1), 61.5);
  const auto second = out.at(2).pitchSlide(note, 62, 64, 2);
  second.sample(out.at(3), 63.5);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
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
  out.pitchSlide(firstNote, 60, 64, 6);
  out.at(3).note(67, 1.0, 3);

  const MidiSequence midi = renderMidiSequence(PerformanceSequence{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
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

void performanceMidiRendererDefersPitchResetUntilTheNextAttack() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 16,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{7}, SourceAnnotationId{8}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId slidingNote = out.note(60, 1.0, 8);
  out.pitchSlide(slidingNote, 60, 64, 4);
  out.at(12).note(67, 1.0, 4);

  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {track},
      },
      {});
  std::vector<std::pair<u64, double>> bends;
  for (const auto& event : lowered.tracks[0].events) {
    if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event)) {
      bends.emplace_back(bend->header.tick, bend->semitones);
    }
  }

  expect(std::ranges::find(bends, std::pair<u64, double>{4, 4.0}) != bends.end() &&
             std::ranges::none_of(bends, [](const auto& bend) { return bend.first == 8; }) &&
             bends.back() == std::pair<u64, double>{12, 0.0},
         "a terminal bend should survive note-off and reset only when the next note attacks");
}

void performanceMidiRendererLeavesTerminalPitchBentWithoutAnotherAttack() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{9}, SourceAnnotationId{10}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId note = out.note(60, 1.0, 8);
  out.pitchSlide(note, 60, 64, 4);

  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {track},
      },
      {});
  const auto& events = lowered.tracks[0].events;
  const auto lastBend = std::find_if(events.rbegin(), events.rend(), [](const PerformanceEvent& event) {
    return std::holds_alternative<PitchBendPerformanceEvent>(event);
  });
  expect(lastBend != events.rend() && std::get<PitchBendPerformanceEvent>(*lastBend).header.tick == 4 &&
             std::get<PitchBendPerformanceEvent>(*lastBend).semitones == 4.0,
         "pitch-bend lowering should not reset a release tail merely because the track has no later attack");
}

void performanceMidiRendererCombinesSourceBendWithPitchTransitions() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{11}, SourceAnnotationId{12}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  out.at(4).pitchBend(0.25);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 64, PitchSlideTiming::fromTicks(0)).continueFrom(first);
  out.at(6).pitchBend(-0.25);
  out.at(8).note(67, 1.0, 4);

  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {track},
      },
      {});
  const auto hasBend = [&](u64 tick, double semitones) {
    return std::ranges::any_of(lowered.tracks[0].events, [&](const PerformanceEvent& event) {
      const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
      return bend != nullptr && bend->header.tick == tick && bend->semitones == semitones;
    });
  };
  expect(hasBend(4, 4.25) && hasBend(6, 3.75) && hasBend(8, -0.25),
         "source bends should stay relative to a held transition and survive its reset at the next attack");

  PerformanceTrack sameVoiceTrack{
      .id = TrackId{1},
      .sourceTrackNumber = 1,
      .endTick = 12,
  };
  PerformanceEmitter sameVoiceOut{sameVoiceTrack, CommandId{13}, SourceAnnotationId{14}, 0,
                                  nextSequence,   nextNote,      nextAutomation};
  const PerformanceNoteId sameVoiceNote = sameVoiceOut.note(64, 1.0, 8);
  sameVoiceOut.pitchBend(1.0);
  sameVoiceOut.at(4).pitchSlide(sameVoiceNote, 65, 67, 2);
  sameVoiceOut.at(7).pitchBend(-1.0);
  sameVoiceOut.at(8).note(60, 1.0, 4);
  const PerformanceSequence sameVoiceLowered = lowerMidiPerformanceAutomation(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {sameVoiceTrack},
      },
      {});
  const auto sameVoiceStart =
      std::ranges::find_if(sameVoiceLowered.tracks[0].events, [](const PerformanceEvent& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend != nullptr && bend->header.tick == 4;
      });
  const auto sourceTakeover =
      std::ranges::find_if(sameVoiceLowered.tracks[0].events, [](const PerformanceEvent& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend != nullptr && bend->header.tick == 7 && bend->semitones == -1.0;
      });
  const auto resetAtNextAttack =
      std::ranges::find_if(sameVoiceLowered.tracks[0].events, [](const PerformanceEvent& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend != nullptr && bend->header.tick == 8 && bend->semitones == 0.0;
      });
  expect(sameVoiceStart != sameVoiceLowered.tracks[0].events.end() &&
             std::get<PitchBendPerformanceEvent>(*sameVoiceStart).semitones == 1.0 &&
             sourceTakeover != sameVoiceLowered.tracks[0].events.end() &&
             resetAtNextAttack == sameVoiceLowered.tracks[0].events.end(),
         "a same-voice transition should replace its starting bend and yield to a later source bend");
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
  out.pitchSlide(firstNote, 60, 68, 8).continueAcrossNotes();
  out.at(4).note(67, 1.0, 4);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
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

void performanceMidiRendererHonorsSpecifiedLfoWaveform() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 4,
          .events =
              {
                  TempoPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .microsecondsPerQuarter = 1'000'000,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .amount = 1.0,
                      .frequencyHz = 12.5,
                      .waveform = LfoWaveform::Sine,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .amount = 0.5,
                      .pitchDepthSemitones = 1.0,
                      .waveform = LfoWaveform::Sine,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .key = 60,
                      .linearVelocity = 1.0,
                      .durationTicks = 4,
                  },
              },
      }},
  };

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto atTickTwo = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    const auto* bend = std::get_if<PitchBend>(&event);
    return bend != nullptr && bend->tick == 2;
  });
  expect(atTickTwo != midi.tracks[0].events.end() && std::get<PitchBend>(*atTickTwo).value == 2896,
         "sequence-event simulation should evaluate an explicitly requested sine LFO");
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
         "global tempo output should be written once on the first MIDI track");
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
                      .frequencyHz = 25.0,
                      .waveform = LfoWaveform::Triangle,
                      .initialPhaseCycles = 0.75,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .target = ModulationPerformanceTarget::TremoloDepth,
                      .volumeDepthDecibels = 3.0,
                      .waveform = LfoWaveform::Triangle,
                      .initialPhaseCycles = 0.75,
                      .tremoloGainMode = TremoloGainMode::NoBoost,
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
    if (const auto* expression = std::get_if<Expression>(&event)) {
      expressions.emplace_back(expression->tick, expression->value);
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

void exportRequestSequenceLoopsAffectMidiLowering() {
  expect(ExportRequest{}.sequence.sequenceLoops == 1,
         "the user-facing export request should default to one sequence loop");
  expect(ExportRequest{}.modulationConversion == ModulationConversionPolicy::SynthModulators,
         "collection export should default to native synth modulation");
  expect(PlaybackRequest{}.modulationConversion == ModulationConversionPolicy::SynthModulators,
         "backend-neutral playback requests should default to native synth modulation");

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

void physicalModulationProfileDrivesMidiAndSynthFromOnePlan() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 24,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{0}, SourceAnnotationId{0}, 0, nextSequence, nextNote, nextAutomation};

  out.vibratoRate(2.0);
  out.vibratoDepth(0.5);
  out.vibratoDelayPhysical(0, 0.0);
  out.tremoloRate(4.0);
  out.tremoloDepth(3.0, LfoPerformanceContext{.tremoloGainMode = TremoloGainMode::NoBoost});
  out.tremoloDelayPhysical(10, 200.0);
  out.at(12).vibratoRate(8.0);
  out.at(12).vibratoDepth(2.0);
  out.at(12).vibratoDelayPhysical(20, 400.0);
  out.at(12).tremoloRate(16.0);
  out.at(12).tremoloDepth(12.0, LfoPerformanceContext{.tremoloGainMode = TremoloGainMode::NoBoost});
  out.at(12).tremoloDelayPhysical(40, 800.0);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };
  const SequenceModulationProfile profile = analyzeSequenceModulation(performance);

  expect(track.hasPhysicalModulation && profile.instruments.vibrato && profile.instruments.tremolo,
         "physical LFO authoring should opt the track into one shared sequence plan");
  expect(profile.instruments.vibrato->maxDepthCents == 200.0 &&
             profile.instruments.vibrato->rateHertz.minimum == 2.0 &&
             profile.instruments.vibrato->rateHertz.maximum == 8.0 &&
             profile.instruments.vibrato->delaySeconds &&
             profile.instruments.vibrato->delaySeconds->minimum == 0.0 &&
             profile.instruments.vibrato->delaySeconds->maximum == 0.4,
         "the shared plan should preserve physical vibrato depth, rate, and delay");
  expect(profile.instruments.tremolo->maxDepthDb == 12.0 &&
             profile.instruments.tremolo->rateHertz.minimum == 4.0 &&
             profile.instruments.tremolo->rateHertz.maximum == 16.0 &&
             profile.instruments.tremolo->gainMode == TremoloGainMode::NoBoost,
         "the shared plan should preserve physical tremolo behavior");

  const MidiSequence midi = renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators);
  u8 firstVibratoDepth = 255;
  u8 lastVibratoDepth = 0;
  u8 firstVibratoRate = 255;
  u8 lastVibratoRate = 0;
  u32 firstVibratoDelay = 255;
  u32 lastVibratoDelay = 0;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* depth = std::get_if<VibratoDepth>(&event)) {
      if (depth->tick == 0) {
        firstVibratoDepth = depth->value;
      } else if (depth->tick == 12) {
        lastVibratoDepth = depth->value;
      }
    } else if (const auto* rate = std::get_if<VibratoFrequency>(&event)) {
      if (rate->tick == 0) {
        firstVibratoRate = rate->value;
      } else if (rate->tick == 12) {
        lastVibratoRate = rate->value;
      }
    } else if (const auto* delay = std::get_if<VibratoDelay>(&event)) {
      if (delay->tick == 0) {
        firstVibratoDelay = delay->ticks;
      } else if (delay->tick == 12) {
        lastVibratoDelay = delay->ticks;
      }
    }
  }
  expect(firstVibratoDepth == 32 && lastVibratoDepth == 127 && firstVibratoRate == 0 &&
             lastVibratoRate == 127 && firstVibratoDelay == 0 && lastVibratoDelay == 127,
         "MIDI controls should normalize the sequence plan while retaining its full useful resolution");

  InstrumentSetAsset instrumentSet{
      .instruments = {Instrument{}},
  };
  applySequenceModulation(instrumentSet, profile);
  const auto& appliedModulation = instrumentSet.instruments[0].modulation;
  expect(appliedModulation.vibrato && appliedModulation.tremolo &&
             appliedModulation.vibrato->maxDepthCents == profile.instruments.vibrato->maxDepthCents &&
             appliedModulation.vibrato->rateHertz.minimum == profile.instruments.vibrato->rateHertz.minimum &&
             appliedModulation.tremolo->maxDepthDb == profile.instruments.tremolo->maxDepthDb &&
             appliedModulation.tremolo->gainMode == profile.instruments.tremolo->gainMode,
         "synth preparation should apply the exact same physical plan used by MIDI");
  const LoweredSynthModulation lowered = lowerSynthModulation(appliedModulation);
  expect(std::ranges::any_of(lowered.modulators, [](const SynthModulator& modulator) {
           return modulator.destination == SynthDestination::VibratoDepth && modulator.amount == 200;
         }) &&
             std::ranges::any_of(lowered.modulators, [](const SynthModulator& modulator) {
               return modulator.destination == SynthDestination::TremoloDepth && modulator.amount == 120;
             }),
         "synth lowering should retain the physical depths chosen by the shared plan");

  PerformanceTrack ordinaryTrack{
      .events = {ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 9.0,
      }},
  };
  expect(analyzeSequenceModulation(PerformanceSequence{.tracks = {ordinaryTrack}}).empty(),
         "physical modulation analysis should return immediately for tracks that did not opt in");
}

void tempoRelativeModulationFollowsTheGlobalTempoTimeline() {
  PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .initialTempoMicrosecondsPerQuarter = 1'000'000,
      .tracks =
          {
              PerformanceTrack{
                  .id = TrackId{0},
                  .sourceTrackNumber = 0,
                  .endTick = 40,
                  .hasPhysicalModulation = true,
                  .events =
                      {
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 0},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .cyclesPerTick = 0.25,
                          },
                          VibratoDelayPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 1},
                              .delayTicks = 10,
                              .tempoRelative = true,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 20, .sequence = 4},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .frequencyHz = 7.0,
                          },
                      },
              },
              PerformanceTrack{
                  .id = TrackId{1},
                  .sourceTrackNumber = 1,
                  .endTick = 40,
                  .events =
                      {
                          TempoPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{1}, .tick = 10, .sequence = 2},
                              .microsecondsPerQuarter = 500'000,
                          },
                          TempoPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{1}, .tick = 30, .sequence = 5},
                              .microsecondsPerQuarter = 250'000,
                          },
                      },
              },
          },
  };

  resolveTempoRelativeModulation(performance);
  std::vector<const ModulationPerformanceEvent*> rates;
  std::vector<const VibratoDelayPerformanceEvent*> delays;
  for (const PerformanceEvent& event : performance.tracks[0].events) {
    if (const auto* rate = std::get_if<ModulationPerformanceEvent>(&event)) {
      rates.push_back(rate);
    }
    if (const auto* delay = std::get_if<VibratoDelayPerformanceEvent>(&event)) {
      delays.push_back(delay);
    }
  }

  expect(rates.size() == 3 && rates[0]->header.tick == 0 && rates[0]->frequencyHz &&
             std::abs(*rates[0]->frequencyHz - 25.0) < 0.0001 && rates[1]->header.tick == 10 &&
             rates[1]->frequencyHz && std::abs(*rates[1]->frequencyHz - 50.0) < 0.0001 &&
             rates[1]->header.sequence == 2 && rates[2]->header.tick == 20 && rates[2]->frequencyHz &&
             std::abs(*rates[2]->frequencyHz - 7.0) < 0.0001,
         "tempo-relative LFO rates should follow cross-track tempo changes in global execution order");
  expect(delays.size() == 3 && delays[0]->milliseconds &&
             std::abs(*delays[0]->milliseconds - 100.0) < 0.0001 && delays[1]->header.tick == 10 &&
             delays[1]->milliseconds && std::abs(*delays[1]->milliseconds - 50.0) < 0.0001 &&
             delays[2]->header.tick == 30 && delays[2]->milliseconds &&
             std::abs(*delays[2]->milliseconds - 25.0) < 0.0001 &&
             std::ranges::all_of(delays, [](const auto* delay) { return delay->tempoRelative; }),
         "tempo-relative LFO delays should retain ticks while exposing physical synth delay values");

  const auto simulatedPitchBends = [](bool changeTempo) {
    PerformanceSequence simulation{
        .timebase = Timebase{.ppqn = 100},
        .initialTempoMicrosecondsPerQuarter = 1'000'000,
        .tracks =
            {
                PerformanceTrack{
                    .id = TrackId{0},
                    .sourceTrackNumber = 0,
                    .endTick = 8,
                    .hasPhysicalModulation = true,
                    .events =
                        {
                            ModulationPerformanceEvent{
                                .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 0},
                                .target = ModulationPerformanceTarget::VibratoRate,
                                .cyclesPerTick = 0.125,
                            },
                            ModulationPerformanceEvent{
                                .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 1},
                                .target = ModulationPerformanceTarget::VibratoDepth,
                                .pitchDepthSemitones = 1.0,
                            },
                            NotePerformanceEvent{
                                .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 2},
                                .key = 60.0,
                                .linearVelocity = 1.0,
                                .durationTicks = 8,
                            },
                        },
                },
                PerformanceTrack{
                    .id = TrackId{1},
                    .sourceTrackNumber = 1,
                    .endTick = 8,
                },
            },
    };
    if (changeTempo) {
      simulation.tracks[1].events.emplace_back(TempoPerformanceEvent{
          .header = PerformanceEventHeader{.track = TrackId{1}, .tick = 4, .sequence = 3},
          .microsecondsPerQuarter = 500'000,
      });
    }
    resolveTempoRelativeModulation(simulation);
    const MidiSequence midi =
        renderMidiSequence(simulation, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
    std::vector<std::pair<u64, s16>> result;
    for (const MidiEvent& event : midi.tracks[0].events) {
      if (const auto* bend = std::get_if<PitchBend>(&event)) {
        result.emplace_back(bend->tick, bend->value);
      }
    }
    return result;
  };
  expect(simulatedPitchBends(true) == simulatedPitchBends(false),
         "sequence-event simulation should preserve a sequence-clocked LFO's exact phase across tempo changes");
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
  midiExporterKeepsZeroDurationNotePairedAtSameTick();
  performanceMidiRendererTrustsSourceNoteExtensions();
  performanceMidiRendererWritesTimeSignaturesToFirstTrack();
  performanceMidiRendererWritesPanGainResetWhenRequested();
  performanceMidiRendererCombinesExpressionWithPanGain();
  performanceMidiRendererLowersDeclaredPanLaws();
  performanceMidiRendererHonorsMidiExportOptions();
  performanceMidiRendererLowersStructuredScalarAutomationPoints();
  performanceMidiRendererSuppressesOnlyAutomationOwnedControllerDuplicates();
  performanceMidiRendererChoosesPitchTransitionRepresentationAtLowering();
  performanceMidiRendererAllowsMixedPitchTransitionRendering();
  performanceMidiRendererRetainsHeldVoiceAcrossChainedPitchBends();
  performanceMidiRendererRetriggersUnlinkedPitchBendDestinations();
  performanceMidiRendererStartsANewVoiceAfterPitchBendContinuationWhenNativePortamentoTakesOver();
  performanceMidiRendererResetsHeldPitchBeforeNativePortamentoTakesOver();
  performanceMidiRendererCombinesPitchSlidesWithSimulatedVibrato();
  performanceMidiRendererUsesOnlyFrozenVibratoOffsetForPitchRange();
  performanceMidiRendererUsesWholeSemitonePitchBendRanges();
  performanceMidiRendererDoesNotRestartVibratoAtAHeldPitchSlideBoundary();
  performanceMidiRendererPreservesExactSamplesAndChainedPitchContinuity();
  performanceMidiRendererResetsInterruptedPitchBeforeTheNewNote();
  performanceMidiRendererDefersPitchResetUntilTheNextAttack();
  performanceMidiRendererLeavesTerminalPitchBentWithoutAnotherAttack();
  performanceMidiRendererCombinesSourceBendWithPitchTransitions();
  performanceMidiLoweringCanContinueAnAbsoluteCurveAcrossNewNotes();
  performanceMidiRendererResolvesSourceInstrumentIdentityAtExport();
  performanceMidiRendererQuantizesPitchBendAndPortamento();
  performanceMidiRendererSkipsRedundantPitchBends();
  performanceMidiRendererSimulatesDelayedVibratoAsPitchBendShape();
  performanceMidiRendererHonorsSpecifiedLfoWaveform();
  performanceMidiRendererDoesNotDoubleDelayVibrato();
  performanceMidiRendererRestartsSimulatedVibratoDelayForNewNotes();
  performanceMidiRendererSimulatesTremoloUsingGlobalTempo();
  performanceMidiRendererHonorsNoBoostTremoloPhaseAndResetPolicy();
  exportRequestSequenceLoopsAffectMidiLowering();
  standaloneSequenceExportDoesNotRequireACollection();
  modulationAnalysisReportsObservedPerformanceRanges();
  physicalModulationProfileDrivesMidiAndSynthFromOnePlan();
  tempoRelativeModulationFollowsTheGlobalTempoTimeline();
  observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators();
  observedModulationScalingUsesPreciseNormalizedAmounts();
}

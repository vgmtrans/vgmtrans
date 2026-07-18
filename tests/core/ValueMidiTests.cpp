/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

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

  const auto exported = MidiExporter().exportMidi(midiSequence);
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

  const auto exported = MidiExporter().exportMidi(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x07, 0x24, 0x00, 0xb0, 0x27, 0x34, 0x00, 0xb0, 0x0a, 0x40,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should keep 14-bit volume MSB/LSB controllers adjacent before same-tick pan");
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

  const auto exported = MidiExporter().exportMidi(midiSequence);
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

  const auto exported = MidiExporter().exportMidi(midiSequence);
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

  expect(MidiExporter().exportMidi(midiSequence) == expected, "MIDI exporter should write time-signature meta events");
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

  const auto exported = MidiExporter().exportMidi(midiSequence);
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

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(performance);
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

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(performance);
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

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(performance);
  const auto& events = midiSequence.tracks[0].events;
  expect(std::get<Pan>(events[1]).value == 0 && std::holds_alternative<Expression>(events[2]),
         "pan gain compensation should emit expression with the pan event");
  expect(std::get<Pan>(events[3]).value == 64 && std::get<Expression>(events[4]).value == 127,
         "full-gain compensated pan should reset expression to full scale");
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

  const MidiSequence autoMidi = PerformanceMidiRenderer().render(performance);
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
      PerformanceMidiRenderer().render(performance, MidiExportOptions{
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
      PerformanceMidiRenderer().render(performance, {}, ModulationConversionPolicy::SynthModulators, instrumentSets);
  expect(std::get<BankSelect>(midi.tracks[0].events[1]).bank == 3 &&
             std::get<ProgramChange>(midi.tracks[0].events[2]).program == 9,
         "MIDI lowering should resolve source instrument identities through collection instruments");
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

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(performance);
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

  assertPitchBends(PerformanceMidiRenderer().render(performance), "synth-modulator MIDI lowering");
  assertPitchBends(PerformanceMidiRenderer().render(performance, MidiExportOptions{},
                                                    ModulationConversionPolicy::SequenceEventSimulation),
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

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(
      performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
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

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(
      performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);

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

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(
      performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);

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

void exportRequestSequenceLoopsAffectMidiLowering() {
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

  SessionSnapshotBuilder snapshotBuilder;
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
                                              .loopPolicy = LoopPolicy::PlayOnce,
                                              .sequenceLoops = 2,
                                          },
                                          dialects);

  expect(artifacts.size() == 1 && artifacts[0].diagnostics.empty(),
         "MIDI export with configured sequence loops should produce one clean artifact");
  const auto noteOnCount = std::ranges::count(artifacts[0].bytes, static_cast<u8>(0x90));
  expect(noteOnCount == 3, "ExportRequest sequenceLoops should replay the loop before MIDI rendering");
}

void modulationAnalysisReportsObservedMidiControllerRanges() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              MidiTrack{
                  .name = "Lead",
                  .events =
                      {
                          VibratoDepth{.tick = 0, .channel = 0, .value = 0},
                          VibratoDepth{.tick = 12, .channel = 0, .value = 82},
                          VibratoFrequency{.tick = 12, .channel = 0, .value = 17},
                          TremoloDepth{.tick = 24, .channel = 0, .value = 40},
                          TremoloFrequency{.tick = 24, .channel = 0, .value = 5},
                      },
              },
              MidiTrack{
                  .name = "Pad",
                  .events =
                      {
                          VibratoFrequency{.tick = 0, .channel = 1, .value = 29},
                          TremoloFrequency{.tick = 0, .channel = 1, .value = 9},
                      },
              },
          },
  };

  const auto usage = analyzeMidiModulationUsage(midiSequence);
  expect(hasMidiModulationUsage(usage), "MIDI modulation analysis should report observed controller modulation");
  expect(usage.tracks.size() == 2, "MIDI modulation analysis should preserve track-level results");
  expect(usage.vibratoDepth.observed && usage.vibratoDepth.min == 0 && usage.vibratoDepth.max == 82,
         "MIDI modulation analysis should report global vibrato depth controller range");
  expect(usage.vibratoRate.observed && usage.vibratoRate.min == 17 && usage.vibratoRate.max == 29,
         "MIDI modulation analysis should report global vibrato rate controller range");
  expect(usage.tremoloDepth.observed && usage.tremoloDepth.min == 40 && usage.tremoloDepth.max == 40,
         "MIDI modulation analysis should report global tremolo depth controller range");
  expect(usage.tremoloRate.observed && usage.tremoloRate.min == 5 && usage.tremoloRate.max == 9,
         "MIDI modulation analysis should report global tremolo rate controller range");
  expect(usage.tracks[0].trackIndex == 0 && usage.tracks[0].vibratoDepth.max == 82 &&
             usage.tracks[0].vibratoRate.max == 17,
         "MIDI modulation analysis should keep first track modulation ranges separate");
  expect(usage.tracks[1].trackIndex == 1 && !usage.tracks[1].vibratoDepth.observed &&
             usage.tracks[1].vibratoRate.max == 29,
         "MIDI modulation analysis should keep second track modulation ranges separate");
}

void modulationAnalysisReportsObservedPerformanceRanges() {
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

  const auto usage = analyzePerformanceModulationUsage(performance);
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

  const auto usage = analyzeMidiModulationUsage(midiSequence);
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

  const auto usage = analyzeMidiModulationUsage(midiSequence);
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
  midiExporterOrdersFineTuneBeforeSameTickProgramChange();
  midiExporterKeepsSameTickBankProgramPairsAdjacent();
  midiExporterWritesTimeSignatureMetaEvent();
  midiExporterOrdersGeneratedNoteOffBeforeSameTickNoteOn();
  performanceMidiRendererTrustsSourceNoteExtensions();
  performanceMidiRendererWritesTimeSignaturesToFirstTrack();
  performanceMidiRendererWritesPanGainResetWhenRequested();
  performanceMidiRendererHonorsMidiExportOptions();
  performanceMidiRendererResolvesSourceInstrumentIdentityAtExport();
  performanceMidiRendererQuantizesPitchBendAndPortamento();
  performanceMidiRendererSkipsRedundantPitchBends();
  performanceMidiRendererSimulatesDelayedVibratoAsPitchBendShape();
  performanceMidiRendererDoesNotDoubleDelayVibrato();
  performanceMidiRendererRestartsSimulatedVibratoDelayForNewNotes();
  exportRequestSequenceLoopsAffectMidiLowering();
  modulationAnalysisReportsObservedMidiControllerRanges();
  modulationAnalysisReportsObservedPerformanceRanges();
  observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators();
  observedModulationScalingUsesPreciseNormalizedAmounts();
}

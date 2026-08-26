/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"
#include "../MidiTestSupport.h"

#include "SessionSnapshotBuilder.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"
#include "value/sequence/TempoRelativeModulation.h"

#include <array>

namespace {

void midiExporterWritesStandardMidiFile() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track{.name = "Lead", .endTick = 24};
  track.events = {
      midi::meta(0, 0x51, {0x07, 0xa1, 0x20}),
      midi::meta(0, 0x21, {2}, -5),
      midi::programChange(0, 0, 5),
      midi::controller(0, 0, MidiController::ChannelVolume, 100),
      midi::note(0, 0, 60, 100, 24),
      midi::controller(12, 0, MidiController::Pan, 64),
  };
  midiSequence.tracks.push_back(std::move(track));

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
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track;
  midi::appendController14(track, 0, 0, MidiController::ChannelVolume, 0x1234);
  track.events.push_back(midi::controller(0, 0, MidiController::Pan, 64));
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x07, 0x24, 0x00, 0xb0, 0x27, 0x34, 0x00, 0xb0, 0x0a, 0x40,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should keep 14-bit volume MSB/LSB controllers adjacent before same-tick pan");
}

void midiExporterWritesAllSoundOffImmediatelyBeforeNoteOn() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track{.endTick = 36};
  track.events = {midi::note(12, 2, 60, 100, 24), midi::controller(12, 2, MidiController::AllSoundOff, 0, 45)};
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x0c, 0xb2, 0x78, 0x00, 0x00, 0x92, 0x3c, 0x64,
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should write All Sound Off immediately before the replacement note-on");
}

void midiExporterPreservesLegacyPortamentoTimeByteOrder() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track;
  midi::appendController14(track, 0, 0, MidiController::PortamentoTime, 0x01d3, true);
  track.events.push_back(midi::controller(0, 0, MidiController::PortamentoControl, 60));
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x25, 0x53, 0x00, 0xb0, 0x05, 0x03, 0x00, 0xb0, 0x54, 0x3c,
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should write fine then coarse portamento time before source-key control");
}

void midiExporterOrdersFineTuneBeforeSameTickProgramChange() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track;
  midi::appendRpn(track, 0, 2, 0, 1, 0x1100, 8);
  track.events.push_back(midi::bankSelect(0, 2, 0, false));
  track.events.push_back(midi::programChange(0, 2, 9));
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb2, 0x65, 0x00, 0x00, 0xb2, 0x64, 0x01, 0x00, 0xb2, 0x06, 0x22,
      0x00, 0xb2, 0x26, 0x00, 0x00, 0xb2, 0x00, 0x00, 0x00, 0xc2, 0x09,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should serialize fine tuning RPN before same-tick bank and program changes");
}

void midiExporterKeepsSameTickBankProgramPairsAdjacent() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track{.endTick = 24};
  track.events = {
      midi::bankSelect(0, 0, 0, false), midi::programChange(0, 0, 13), midi::bankSelect(0, 0, 0x7f, false),
      midi::programChange(0, 0, 0),     midi::note(0, 0, 60, 100, 24),
  };
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x00, 0x00, 0x00, 0xc0, 0x0d, 0x00, 0xb0, 0x00, 0x7f, 0x00, 0xc0, 0x00, 0x00, 0x90, 0x3c, 0x64,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should keep same-tick bank/program pairs adjacent before notes");
}

void midiExporterWritesTimeSignatureMetaEvent() {
  const MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48},
                                  .tracks = {MidiTrack{.events = {midi::meta(0, 0x58, {3, 2, 48, 8})}}}};

  const std::vector<u8> expected{
      'M', 'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 'M',  'T',  'r',
      'k', 0x00, 0x00, 0x00, 0x0c, 0x00, 0xff, 0x58, 0x04, 0x03, 0x02, 0x30, 0x08, 0x00, 0xff, 0x2f, 0x00,
  };

  expect(encodeMidiFile(midiSequence) == expected, "MIDI exporter should write time-signature meta events");
}

void midiExporterOrdersGeneratedNoteOffBeforeSameTickNoteOn() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{.events = {midi::note(10, 0, 60, 100, 10), midi::note(0, 0, 60, 100, 10)}, .endTick = 20}}};

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
      .tracks = {MidiTrack{.events = {midi::note(10, 0, 60, 90, 0), midi::note(10, 0, 60, 100, 5)}, .endTick = 15}}};

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

void performanceMidiRendererSelectsWideTuningRepresentation() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .endTick = 18,
          .events =
              {
                  TuningPerformanceEvent{.header = PerformanceEventHeader{.tick = 0}, .cents = 100.0},
                  PitchBendPerformanceEvent{.header = PerformanceEventHeader{.tick = 6}, .semitones = 0.5},
                  TuningPerformanceEvent{.header = PerformanceEventHeader{.tick = 12}, .cents = 214.0625},
                  TuningPerformanceEvent{.header = PerformanceEventHeader{.tick = 18}, .cents = 14.0625},
              },
      }},
  };

  const MidiSequence compatible = renderMidiSequence(performance);
  std::vector<u16> compatibleFine;
  std::vector<std::pair<u64, s32>> compatibleBends;
  size_t compatibleCoarseCount = 0;
  for (const auto& rpn : midiRpns(compatible.tracks.front().events)) {
    if (rpn.parameterMsb == 0 && rpn.parameterLsb == 1) {
      compatibleFine.push_back(rpn.value);
    } else if (rpn.parameterMsb == 0 && rpn.parameterLsb == 2) {
      ++compatibleCoarseCount;
    }
  }
  for (const auto& event : compatible.tracks.front().events) {
    if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      compatibleBends.emplace_back(event.tick, bend->value);
    }
  }
  expect(compatibleCoarseCount == 0 && compatibleFine == std::vector<u16>{16383, 16383, 9344},
         "compatible tuning should keep the fine-tuning range out of coarse RPN");
  expect(compatibleBends == std::vector<std::pair<u64, s32>>{{6, 2048}, {12, 6720}, {18, 2048}},
         "compatible wide tuning should add its excess to source pitch bend and remove it when tuning narrows");

  MidiExportOptions coarseOptions;
  coarseOptions.wideTuning = MidiWideTuningRendering::CoarseTune;
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
             isMidiController(events[2], MidiController::Expression),
         "pan gain compensation should emit expression with the pan event");
  expect(midiController(events[3], MidiController::Pan)->value == 64 &&
             midiController(events[4], MidiController::Expression)->value == 127,
         "full-gain compensated pan should reset expression to full scale");
  expect(midiController(events[5], MidiController::Pan)->value == 64 &&
             midiController(events[6], MidiController::Expression)->value == 107,
         "MIDI pan lowering should preserve a phase-inverted channel's magnitude");
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
      if (const auto* expression = midiController(event, MidiController::Expression)) {
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
  std::get<ExpressionPerformanceEvent>(precisePerformance.tracks[0].events[0]).sourceQuantization =
      ValueQuantization{.levels = 256};
  const MidiSequence preciseMidi = renderMidiSequence(precisePerformance);
  expect(
      std::count_if(preciseMidi.tracks[0].events.begin(), preciseMidi.tracks[0].events.end(),
                    [](const MidiEvent& event) {
                      return isMidiControllerLsb(event, MidiController::Expression);
                    }) == 4,
         "pan compensation should preserve the source expression's quantization");
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
  const auto constantSumExpression = std::ranges::find_if(midi.tracks[1].events, [](const MidiEvent& event) {
    return isMidiController(event, MidiController::Expression);
  });
  expect(constantSumExpression != midi.tracks[1].events.end() &&
             midiController(*constantSumExpression, MidiController::Expression)->value == 107,
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
                      .hasLinearGain = true,
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
                                   const auto* expression = midiController(event, MidiController::Expression);
                                   return expression && event.tick == 2 && expression->value == 127;
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
                                                      .bank = 130,
                                                      .program = 5,
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
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 10, .sequence = 2},
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
  expect(std::ranges::count_if(terminated.tracks[0].events, isSoundOff) == 1 &&
             soundOff != terminated.tracks[0].events.end() && soundOff->tick == 8,
         "the renderer should terminate a previous voice before a fresh attack, but not before an extension");
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
  expect(std::ranges::count_if(
             midi.tracks[0].events,
             [](const MidiEvent& event) { return isMidiController(event, MidiController::ChannelVolume); }) == 1 &&
             std::ranges::count_if(
                 midi.tracks[0].events,
                 [](const MidiEvent& event) { return isMidiController(event, MidiController::Expression); }) == 1 &&
             std::ranges::count_if(
                 midi.tracks[0].events,
                 [](const MidiEvent& event) { return isMidiController(event, MidiController::Pan); }) == 1,
         "automation lowering should suppress repeated quantized volume, expression, and pan values");

  PerformanceSequence flatPerformance = performance;
  auto& flatTrack = flatPerformance.tracks.front();
  for (auto& event : flatTrack.events) {
    std::visit([](auto& typed) { typed.header.automation.reset(); }, event);
  }
  flatTrack.automations.clear();
  const MidiSequence flatMidi = renderMidiSequence(flatPerformance);
  expect(std::ranges::count_if(
             flatMidi.tracks[0].events,
             [](const MidiEvent& event) { return isMidiController(event, MidiController::ChannelVolume); }) == 2 &&
             std::ranges::count_if(
                 flatMidi.tracks[0].events,
                 [](const MidiEvent& event) { return isMidiController(event, MidiController::Expression); }) == 2 &&
             std::ranges::count_if(
                 flatMidi.tracks[0].events,
                 [](const MidiEvent& event) { return isMidiController(event, MidiController::Pan); }) == 2,
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

  PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track, rateTrack, fixedTrack},
  };
  auto sourceAttack = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  std::get<NotePerformanceEvent>(*sourceAttack).instrumentAddress = InstrumentAddress{.bank = 3, .program = 4};

  const MidiSequence native = renderMidiSequence(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  const auto portamentoTime = firstMidiController14(native.tracks[0].events, MidiController::PortamentoTime);
  expect(portamentoTime == 63 &&
             std::ranges::any_of(native.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bank = std::get_if<BankSelect>(&event.payload);
                                   return bank != nullptr && bank->bank == 3;
                                 }) &&
             std::ranges::any_of(
                 native.tracks[0].events,
                 [](const MidiEvent& event) { return isMidiController(event, MidiController::PortamentoControl); }) &&
             std::ranges::none_of(
                 native.tracks[0].events,
                 [](const MidiEvent& event) { return isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend); }),
         "portamento lowering should derive physical duration from sequence ticks and tempo");
  const auto rateTime = firstMidiController14(native.tracks[1].events, MidiController::PortamentoTime);
  expect(rateTime == 2000,
         "fixed-rate timing should derive portamento duration from pitch distance independently of tempo");
  const auto fixedTime = firstMidiController14(native.tracks[2].events, MidiController::PortamentoTime);
  expect(fixedTime == 125, "fixed-duration timing should preserve source physical time independently of tempo");

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
           lhs.extendsPrevious == rhs.extendsPrevious && lhs.instrumentAddress == rhs.instrumentAddress &&
           lhs.restartsLfoPhase == rhs.restartsLfoPhase && lhs.restartsVibratoLfoPhase == rhs.restartsVibratoLfoPhase &&
           lhs.restartsTremoloLfoPhase == rhs.restartsTremoloLfoPhase && lhs.note == rhs.note && lhs.lane == rhs.lane;
  };
  expect(sourceNote != performance.tracks[0].events.end() && loweredNote != bendLowering.tracks[0].events.end() &&
             notesMatch(std::get<NotePerformanceEvent>(*sourceNote), std::get<NotePerformanceEvent>(*loweredNote)),
         "pitch-bend lowering should preserve the source note event verbatim");
  const auto noteEvent = std::ranges::find_if(bent.tracks[0].events, [](const MidiEvent& event) {
    return std::holds_alternative<NoteDuration>(event.payload);
  });
  expect(
      noteEvent != bent.tracks[0].events.end() && std::get<NoteDuration>(noteEvent->payload).key == 64 &&
          std::ranges::any_of(midiRpns(bent.tracks[0].events),
                              [](const MidiRpnView& rpn) { return rpn.parameterMsb == 0 && rpn.parameterLsb == 0; }) &&
          std::ranges::any_of(
              bent.tracks[0].events,
              [](const MidiEvent& event) { return isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend); }) &&
          std::ranges::none_of(bent.tracks[0].events,
                               [](const MidiEvent& event) {
                                 return isMidiController(event, MidiController::PortamentoTime) ||
                                        isMidiControllerLsb(event, MidiController::PortamentoTime) ||
                                        isMidiController(event, MidiController::PortamentoControl);
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
    return std::ranges::count_if(midi.tracks[0].events, [](const MidiEvent& event) {
      return isMidiController(event, MidiController::PortamentoControl);
    });
  };
  const auto countPitchBends = [](const MidiSequence& midi) {
    return std::ranges::count_if(midi.tracks[0].events, [](const MidiEvent& event) {
      return isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend);
    });
  };
  const auto noteDuration = [](const MidiSequence& midi, u64 tick) -> std::optional<u32> {
    const auto found = std::ranges::find_if(midi.tracks[0].events, [tick](const MidiEvent& event) {
      const auto* note = std::get_if<NoteDuration>(&event.payload);
      return note != nullptr && event.tick == tick;
    });
    return found == midi.tracks[0].events.end() ? std::nullopt
                                                : std::optional{std::get<NoteDuration>(found->payload).duration};
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

  const MidiSequence terminatingPortamento = renderMidiSequence(
      performance,
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento, .terminatePreviousVoice = true});
  expect(
      std::ranges::none_of(terminatingPortamento.tracks[0].events,
                           [](const MidiEvent& event) { return isMidiController(event, MidiController::AllSoundOff); }),
         "new-attack termination should not cut off linked native-portamento continuations");

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
      .endTick = 16,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{1}, SourceAnnotationId{2}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  out.pitchSlide(first, 56, 60, 8);
  const PerformanceNoteId second = out.at(4).note(62, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 62, 8).continueFrom(first);
  const PerformanceNoteId third = out.at(8).note(64, 1.0, 4);
  out.at(8).pitchSlide(third, 62, 64, 4).continueFrom(second);
  const PerformanceNoteId fourth = out.at(12).note(67, 1.0, 4);
  out.at(12).pitchSlide(fourth, 64, 67, 4).continueFrom(third);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };
  const MidiExportOptions bendOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend};
  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(performance, bendOptions);
  const MidiSequence midi = renderMidiSequence(performance, bendOptions);

  const auto notes = midiNotes(midi.tracks[0].events);
  expect(notes.size() == 1 && notes[0].tick == 0 && notes[0].key == 60 && notes[0].duration == 16,
         "linked pitch bends should sustain one MIDI note instead of retriggering each destination");

  const auto ranges = midiPitchBendRanges(midi.tracks[0].events);
  expect(ranges == std::vector<std::pair<u64, u16>>{{0, 700}},
         "one linked MIDI voice should keep a stable range large enough for its entire pitch path");
  const auto hasMidiBend = [&](u64 tick, s16 value) {
    return std::ranges::any_of(midi.tracks[0].events, [&](const MidiEvent& event) {
      const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
      return bend != nullptr && event.tick == tick && bend->value == value;
    });
  };
  expect(hasMidiBend(8, 2341) && hasMidiBend(12, 4681) && hasMidiBend(16, 8191),
         "chained transitions should honor each absolute start key without retuning the held voice's bend range");

  const auto chainedStart = std::ranges::find_if(lowered.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 12 && std::abs(bend->semitones - 4.0) < 0.000001;
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
  const auto noteCount = std::ranges::count_if(midi.tracks[0].events, [](const MidiEvent& event) {
    return std::holds_alternative<NoteDuration>(event.payload);
  });
  expect(noteCount == 2, "a boundary pitch slide without continueFrom should retain the destination note's attack");

  const MidiSequence terminatingPortamento = renderMidiSequence(
      PerformanceSequence{.timebase = Timebase{.ppqn = 48}, .tracks = {track}},
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento, .terminatePreviousVoice = true});
  expect(std::ranges::count_if(
             terminatingPortamento.tracks[0].events,
             [](const MidiEvent& event) { return isMidiController(event, MidiController::AllSoundOff); }) == 1,
         "new-attack termination should still cut off an unlinked portamento destination");
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
  const auto notes = midiNotes(midi.tracks[0].events);
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
    const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
    if (bend != nullptr && event.tick == 8) {
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
      .context = LfoPerformanceContext{.frequencyHz = 12.5},
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
      const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
      if (bend != nullptr && event.tick == tick) {
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
                      .context = LfoPerformanceContext{.frequencyHz = 0.0},
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
                      .context = LfoPerformanceContext{.frequencyHz = 1.0},
                  },
              },
      }},
  };

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto pitchBendRanges = midiPitchBendRanges(midi.tracks[0].events);

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
  expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, 300}},
         "MIDI renderer should round pitch-bend ranges upward to whole semitones");
  expect(std::ranges::any_of(events,
                             [](const MidiEvent& event) {
                               const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                               return bend != nullptr && bend->value == 4369;
                             }),
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
      .context = LfoPerformanceContext{.frequencyHz = 25.0},
  });
  out.modulation(ModulationPerformanceEvent{
      .target = ModulationPerformanceTarget::VibratoDepth,
      .amount = 0.5,
      .pitchDepthSemitones = 1.0,
  });
  out.vibratoDelayTicks(6);
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
      if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
          bend != nullptr && event.tick == tick) {
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

  const auto ranges = midiPitchBendRanges(midi.tracks[0].events);
  std::vector<std::pair<u64, s16>> bends;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      bends.emplace_back(event.tick, bend->value);
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
    if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
      bends.emplace_back(event.tick, bend->value);
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
  const PerformanceNoteId slidingNote = out.note(60, 1.0, 2);
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

void performanceMidiLoweringAppliesPitchResetsBeforeLaterTransitions() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 30,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{8}, SourceAnnotationId{9}, 0, nextSequence, nextNote, nextAutomation};
  const PerformanceNoteId oldVoice = out.note(60, 1.0, 4);
  out.pitchSlide(oldVoice, 60, 56, 4);
  const PerformanceNoteId heldStart = out.at(8).note(68, 1.0, 2);
  const PerformanceNoteId heldTarget = out.at(10).note(70, 1.0, 20);
  out.at(10).pitchSlide(heldTarget, 68, 70, 5).continueFrom(heldStart);
  out.at(16).pitchSlide(heldTarget, 70, 48, 4);

  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {track},
      },
      {});
  const auto bendAt = [&](u64 tick) -> std::optional<double> {
    std::optional<double> bend;
    for (const auto& event : lowered.tracks[0].events) {
      if (const auto* candidate = std::get_if<PitchBendPerformanceEvent>(&event);
          candidate != nullptr && candidate->header.tick == tick) {
        bend = candidate->semitones;
      }
    }
    return bend;
  };

  expect(bendAt(10) == 0.0 && bendAt(15) == 2.0 && bendAt(16) == 2.0,
         "a delayed transition should observe earlier next-attack resets without doubling a held transition");
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
  out.pitchBendRange(12);
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  out.at(4).pitchBend(0.25);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 64, PitchSlideTiming::fromTicks(0)).continueFrom(first);
  out.at(6).pitchBend(-0.25);
  out.at(6).pitchBendRange(8);
  out.at(8).note(67, 1.0, 4);
  out.at(8).pitchBendRange(6);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .tracks = {track},
  };
  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(performance, {});
  const MidiSequence midi = renderMidiSequence(performance);
  const auto hasBend = [&](u64 tick, double semitones) {
    return std::ranges::any_of(lowered.tracks[0].events, [&](const PerformanceEvent& event) {
      const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
      return bend != nullptr && bend->header.tick == tick && bend->semitones == semitones;
    });
  };
  const auto ranges = midiPitchBendRanges(midi.tracks[0].events);
  const auto hasRange = [&](u64 tick, u16 cents) {
    return std::ranges::find(ranges, std::pair{tick, cents}) != ranges.end();
  };
  expect(hasBend(4, 4.25) && hasBend(6, 3.75) && hasBend(8, -0.25) && hasRange(0, 500) && !hasRange(6, 800) &&
             hasRange(8, 600),
         "a held transition should mask source range changes until the next physical attack");

  PerformanceTrack delayedTransitionTrack{
      .id = TrackId{1},
      .sourceTrackNumber = 1,
      .endTick = 12,
  };
  PerformanceEmitter delayedTransitionOut{delayedTransitionTrack, CommandId{13}, SourceAnnotationId{14}, 0,
                                          nextSequence,           nextNote,      nextAutomation};
  delayedTransitionOut.pitchBendRange(2);
  delayedTransitionOut.note(60, 1.0, 4);
  delayedTransitionOut.at(2).pitchBend(0.25);
  const PerformanceNoteId delayedTransitionNote = delayedTransitionOut.at(4).note(62, 1.0, 8);
  delayedTransitionOut.at(6).pitchSlide(delayedTransitionNote, 62, 72, 2);
  const PerformanceSequence delayedTransitionPerformance{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .tracks = {delayedTransitionTrack},
  };
  for (const auto policy :
       {ModulationConversionPolicy::SynthModulators, ModulationConversionPolicy::SequenceEventSimulation}) {
    const MidiSequence delayedTransitionMidi = renderMidiSequence(delayedTransitionPerformance, {}, policy);
    const auto preservedBend = std::ranges::find_if(delayedTransitionMidi.tracks[0].events, [](const MidiEvent& event) {
      const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
      return bend != nullptr && event.tick == 4 && bend->value == 205;
        });
    const auto delayedTransitionRanges = midiPitchBendRanges(delayedTransitionMidi.tracks[0].events);
    expect(std::ranges::find(delayedTransitionRanges, std::pair<u64, u16>{4, 1000}) != delayedTransitionRanges.end() &&
               preservedBend != delayedTransitionMidi.tracks[0].events.end(),
           "changing sensitivity for a later transition should re-encode an active bend at the new voice attack");
  }

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
          .events =
              {
                  InstrumentPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .sourceInstrument = InstrumentIdentity{.domain = "probe.instrument", .key = 5},
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 1},
                      .key = 60.0,
                      .durationTicks = 4,
                  },
                  PitchBendPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 2, .automation = PerformanceAutomationId{0}},
                      .semitones = -0.09375,
                      .normalizedWheelPosition = -0.046875,
                  },
              },
      }},
  };
  const SoundBankAsset soundBank{
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 3, .program = 9},
          .identity = InstrumentIdentity{.domain = "probe.instrument", .key = 5},
          .pitchBendRangeCents = 2400,
      }},
  };
  const std::array<const SoundBankAsset*, 1> soundBanks{&soundBank};

  const MidiSequence midi =
      renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);
  const auto bank = std::ranges::find_if(midi.tracks[0].events,
                                         [](const MidiEvent& event) { return midiBankSelect(event) != nullptr; });
  const auto program = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    return isMidiChannelMessage(event, MidiChannelMessageKind::ProgramChange);
  });
  const auto bend = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    return isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend);
  });
  const auto ranges = midiPitchBendRanges(midi.tracks[0].events);
  expect(bank != midi.tracks[0].events.end() && midiBankSelect(*bank)->bank == 3 &&
             program != midi.tracks[0].events.end() &&
             midiChannelMessage(*program, MidiChannelMessageKind::ProgramChange)->value == 9 &&
             ranges == std::vector<std::pair<u64, u16>>{{0, 2400}} && bend != midi.tracks[0].events.end() &&
             midiChannelMessage(*bend, MidiChannelMessageKind::PitchBend)->value == -384,
         "an automated bend should retain the selected instrument's pitch-wheel sensitivity");

  const MidiSequence mmaMidi =
      renderMidiSequence(performance, MidiExportOptions{.bankSelectStyle = MidiBankSelectStyle::MsbAndLsb},
                         ModulationConversionPolicy::SynthModulators, soundBanks);
  const auto mmaBank = std::ranges::find_if(mmaMidi.tracks[0].events,
                                            [](const MidiEvent& event) { return midiBankSelect(event) != nullptr; });
  expect(mmaBank != mmaMidi.tracks[0].events.end() && midiBankSelect(*mmaBank)->bank == 3,
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
  expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, 400}},
         "MIDI renderer should emit the performance pitch-bend range");
  expect(std::ranges::any_of(events,
                             [](const MidiEvent& event) {
                               const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                               return bend != nullptr && event.tick == 0 && bend->value == 2048;
                             }),
         "MIDI renderer should quantize semitone pitch bend through the active range");
  expect(std::ranges::any_of(events,
                             [](const MidiEvent& event) {
                               const auto* time = midiController(event, MidiController::PortamentoTime);
                               return time != nullptr && event.tick == 12 && time->value == 83;
                             }),
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
    const auto pitchBendRanges = midiPitchBendRanges(midiSequence.tracks[0].events);
    std::vector<std::pair<u64, s16>> pitchBends;
    for (const MidiEvent& event : midiSequence.tracks[0].events) {
      if (const auto* pitchBend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
        pitchBends.emplace_back(event.tick, pitchBend->value);
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
                  VibratoDelayPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .delayTicks = 2,
                  },
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
                  VibratoDelayPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .delayTicks = 2,
                  },
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
                  VibratoDelayPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .delayTicks = 2,
                  },
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

void performanceMidiRendererReplacesSavedNoteDelay() {
  const LfoShape shape{.waveform = LfoWaveform::Sine, .samples = {0.0, 1.0, 0.0, -1.0}};
  std::vector<PerformanceEvent> events{
      VibratoDelayPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 0},
          .delayTicks = 3,
          .updateMode = LfoDelayUpdateMode::FutureNotesOnly,
      },
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
      VibratoDelayPerformanceEvent{
          .header = PerformanceEventHeader{.tick = 4},
          .delayTicks = 1,
      },
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
                          TremoloDelayPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .delayTicks = 2,
                          },
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

void exportRequestSequenceLoopsAffectMidiLowering() {
  expect(ExportRequest{}.sequence.sequenceLoops == 1,
         "the user-facing export request should default to one sequence loop");
  expect(ExportRequest{}.modulationConversion == ModulationConversionPolicy::SynthModulators,
         "collection export should default to native synth modulation");
  expect(PlaybackRequest{}.modulationConversion == ModulationConversionPolicy::SynthModulators,
         "backend-neutral playback requests should default to native synth modulation");
  expect(ExportRequest{}.dynamicEnvelopes == DynamicEnvelopePolicy::Ignore &&
             PlaybackRequest{}.dynamicEnvelopes == DynamicEnvelopePolicy::Ignore,
         "dynamic envelope materialization should remain explicitly opt-in");
  expect(ExportRequest{}.sampleFiltering == SampleFilteringPolicy::FormatPreferred &&
             PlaybackRequest{}.sampleFiltering == SampleFilteringPolicy::FormatPreferred,
         "sample filtering should use each format's recommendation by default");
  expect(!ExportRequest{}.sequence.midi.terminatePreviousVoice,
         "previous-voice termination should remain explicitly opt-in");
  expect(ExportRequest{}.sequence.midi.wideTuning == MidiWideTuningRendering::PitchBend,
         "wide tuning should default to compatible pitch-bend rendering");

  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x00, 0x00};
  addProbeCommand<ProbeNoteCommand>(track, config, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(track, config, Address{3}, probeRange(3, jumpBytes.size()), jumpBytes);

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
              .runtime = probeSequenceRuntime(),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  });
  snapshotBuilder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Looping",
      .members = {.sequence = AssetId{0}},
  });
  const SessionSnapshot project = snapshotBuilder.finish();

  SourceStore sources;

  const auto artifacts = exportCollection(project, sources, CollectionId{0},
                                          ExportRequest{
                                              .kinds = {ExportKind::Midi},
                                              .sequence =
                                                  {
                                                      .loopPolicy = LoopPolicy::PlayOnce,
                                                      .sequenceLoops = 2,
                                                  },
                                          });

  expect(artifacts.size() == 1 && artifacts[0].diagnostics.empty(),
         "MIDI export with configured sequence loops should produce one clean artifact");
  const auto noteOnCount = std::ranges::count(artifacts[0].bytes, static_cast<u8>(0x90));
  expect(noteOnCount == 3, "ExportRequest sequenceLoops should replay the loop before MIDI rendering");
}

void standaloneSequenceExportDoesNotRequireACollection() {
  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(track, config, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeEndCommand>(track, config, Address{3}, probeRange(3, endBytes.size()), endBytes);

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
              .runtime = probeSequenceRuntime(),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  });
  auto ambiguousBuilder = snapshotBuilder;
  ambiguousBuilder.collections = {
      Collection{.id = CollectionId{0}, .name = "First", .members = {.sequence = AssetId{7}}},
      Collection{.id = CollectionId{1}, .name = "Second", .members = {.sequence = AssetId{7}}},
  };
  const SessionSnapshot snapshot = snapshotBuilder.finish();
  expect(snapshot.collections().empty(), "standalone MIDI fixture should not contain a collection");

  const SourceStore sources;
  const Artifact artifact = exportSequenceMidi(snapshot, sources, AssetId{7}, SequenceExportRequest{});

  expect(artifact.filename == "Loose_Sequence.mid",
         "standalone sequence export should derive a safe filename from sequence metadata");
  expect(artifact.mediaType == "audio/midi", "standalone sequence export should identify Standard MIDI data");
  expect(artifact.diagnostics.empty(), "standalone sequence export should not require collection diagnostics");
  expect(artifact.bytes.size() > 14 && std::string(artifact.bytes.begin(), artifact.bytes.begin() + 4) == "MThd",
         "standalone sequence export should produce a Standard MIDI file");

  const Artifact ambiguous =
      exportSequenceMidi(ambiguousBuilder.finish(), sources, AssetId{7}, SequenceExportRequest{});
  expect(ambiguous.bytes.empty(), "direct sequence export should not choose the first of several collections");
  diagnosticWithMessage(ambiguous.diagnostics,
                        "Sequence belongs to multiple collections; export a specific collection instead");
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

void modulationAnalysisReportsObservedPerformanceMaxima() {
  const auto usage = analyzePerformanceModulationUsage(observedModulationPerformance());
  expect(hasMidiModulationUsage(usage), "performance modulation analysis should report observed driver modulation");
  expect(usage.vibratoDepth && usage.vibratoDepth->controllerValue == 82,
         "performance modulation analysis should report global vibrato depth maximum");
  expect(usage.vibratoRate && usage.vibratoRate->controllerValue == 29,
         "performance modulation analysis should report global vibrato rate maximum");
  expect(usage.tremoloDepth && usage.tremoloDepth->controllerValue == 40,
         "performance modulation analysis should report global tremolo depth maximum");
  expect(usage.tremoloRate && usage.tremoloRate->controllerValue == 9,
         "performance modulation analysis should report global tremolo rate maximum");
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

  out.vibratoRate(2.0, LfoPerformanceContext{.shape = LfoShape{.waveform = LfoWaveform::SawtoothUp}});
  out.vibratoDepth(0.5);
  out.vibratoDelayPhysical(0, 0.0);
  out.tremoloRate(4.0, LfoPerformanceContext{.shape = LfoShape{.waveform = LfoWaveform::Square}});
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
  expect(profile.instruments.vibrato->maxDepthCents == 200.0 && profile.instruments.vibrato->rateHertz.minimum == 2.0 &&
             profile.instruments.vibrato->rateHertz.maximum == 8.0 &&
             profile.instruments.vibrato->waveform == LfoWaveform::SawtoothUp &&
             profile.instruments.vibrato->delaySeconds && profile.instruments.vibrato->delaySeconds->minimum == 0.0 &&
             profile.instruments.vibrato->delaySeconds->maximum == 0.4,
         "the shared plan should preserve physical vibrato depth, rate, and delay");
  expect(profile.instruments.tremolo->maxDepthDb == 12.0 && profile.instruments.tremolo->rateHertz.minimum == 4.0 &&
             profile.instruments.tremolo->rateHertz.maximum == 16.0 &&
             profile.instruments.tremolo->waveform == LfoWaveform::Square &&
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
    if (const auto* depth = midiController(event, MidiController::Modulation)) {
      if (event.tick == 0) {
        firstVibratoDepth = static_cast<u8>(depth->value);
      } else if (event.tick == 12) {
        lastVibratoDepth = static_cast<u8>(depth->value);
      }
    } else if (const auto* rate = midiController(event, MidiController::VibratoRate)) {
      if (event.tick == 0) {
        firstVibratoRate = static_cast<u8>(rate->value);
      } else if (event.tick == 12) {
        lastVibratoRate = static_cast<u8>(rate->value);
      }
    } else if (const auto* delay = midiController(event, MidiController::VibratoDelay)) {
      if (event.tick == 0) {
        firstVibratoDelay = delay->value;
      } else if (event.tick == 12) {
        lastVibratoDelay = delay->value;
      }
    }
  }
  expect(firstVibratoDepth == 32 && lastVibratoDepth == 127 && firstVibratoRate == 0 && lastVibratoRate == 127 &&
             firstVibratoDelay == 0 && lastVibratoDelay == 127,
         "MIDI controls should normalize the sequence plan while retaining its full useful resolution");

  SoundBankAsset soundBank{
      .instruments = {Instrument{}},
  };
  applySequenceModulation(soundBank, profile);
  const auto& appliedModulation = soundBank.instruments[0].modulation;
  expect(appliedModulation.vibrato && appliedModulation.tremolo &&
             appliedModulation.vibrato->maxDepthCents == profile.instruments.vibrato->maxDepthCents &&
             appliedModulation.vibrato->rateHertz.minimum == profile.instruments.vibrato->rateHertz.minimum &&
             appliedModulation.tremolo->maxDepthDb == profile.instruments.tremolo->maxDepthDb &&
             appliedModulation.tremolo->gainMode == profile.instruments.tremolo->gainMode,
         "synth preparation should apply the exact same physical plan used by MIDI");
  const LoweredSynthModulation lowered = lowerSynthModulation(appliedModulation);
  expect(std::ranges::any_of(lowered.modulators,
                             [](const SynthModulator& modulator) {
                               return modulator.destination == SynthDestination::VibratoDepth &&
                                      modulator.amount == 200;
                             }) &&
             std::ranges::any_of(lowered.modulators,
                                 [](const SynthModulator& modulator) {
                                   return modulator.destination == SynthDestination::TremoloDepth &&
                                          modulator.amount == 120;
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
                              .context = LfoPerformanceContext{.cyclesPerTick = 0.25},
                          },
                          VibratoDelayPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 1},
                              .delayTicks = 10,
                              .tempoRelative = true,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 20, .sequence = 4},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .context = LfoPerformanceContext{.frequencyHz = 7.0},
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

  expect(rates.size() == 3 && rates[0]->header.tick == 0 && rates[0]->context.frequencyHz &&
             std::abs(*rates[0]->context.frequencyHz - 25.0) < 0.0001 && rates[1]->header.tick == 10 &&
             rates[1]->context.frequencyHz && std::abs(*rates[1]->context.frequencyHz - 50.0) < 0.0001 &&
             rates[1]->header.sequence == 2 && rates[2]->header.tick == 20 && rates[2]->context.frequencyHz &&
             std::abs(*rates[2]->context.frequencyHz - 7.0) < 0.0001,
         "tempo-relative LFO rates should follow cross-track tempo changes in global execution order");
  expect(delays.size() == 3 && delays[0]->milliseconds && std::abs(*delays[0]->milliseconds - 100.0) < 0.0001 &&
             delays[1]->header.tick == 10 && delays[1]->milliseconds &&
             std::abs(*delays[1]->milliseconds - 50.0) < 0.0001 && delays[2]->header.tick == 30 &&
             delays[2]->milliseconds && std::abs(*delays[2]->milliseconds - 25.0) < 0.0001 &&
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
                                .context = LfoPerformanceContext{.cyclesPerTick = 0.125},
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
      if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend)) {
        result.emplace_back(event.tick, bend->value);
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
                          midi::controller(0, 0, MidiController::Modulation, 0),
                          midi::controller(6, 0, MidiController::Modulation, 41),
                          midi::controller(12, 0, MidiController::Modulation, 82),
                          midi::controller(18, 0, MidiController::VibratoRate, 17),
                          midi::controller(24, 0, MidiController::TremoloDepth, 40),
                          midi::controller(30, 0, MidiController::TremoloRate, 5),
                          midi::controller(36, 0, MidiController::TremoloRate, 9),
                      },
              },
              MidiTrack{
                  .name = "Pad",
                  .events =
                      {
                          midi::controller(0, 1, MidiController::VibratoRate, 29),
                      },
              },
          },
  };

  const auto usage = analyzePerformanceModulationUsage(observedModulationPerformance());
  expect(scaledMidiModulationControllerValue(41, &*usage.vibratoDepth, ModulationScalingPolicy::FullFormatRange) == 41,
         "full-range modulation scaling should leave MIDI controller values unchanged");

  applyMidiModulationScaling(midiSequence, usage, ModulationScalingPolicy::ObservedSequenceRange);

  const auto& leadEvents = midiSequence.tracks[0].events;
  expect(midiController(leadEvents[0], MidiController::Modulation)->value == 0,
         "observed modulation scaling should preserve zero controller values");
  expect(midiController(leadEvents[1], MidiController::Modulation)->value == 64,
         "observed modulation scaling should expand intermediate controller values");
  expect(midiController(leadEvents[2], MidiController::Modulation)->value == 127,
         "observed modulation scaling should expand the observed maximum to full MIDI controller range");
  expect(midiController(leadEvents[3], MidiController::VibratoRate)->value == 74,
         "observed modulation scaling should use global rate range across tracks");
  expect(midiController(leadEvents[4], MidiController::TremoloDepth)->value == 127,
         "observed modulation scaling should expand tremolo depth controllers");
  expect(midiController(leadEvents[5], MidiController::TremoloRate)->value == 71 &&
             midiController(leadEvents[6], MidiController::TremoloRate)->value == 127,
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
                  midi::controller(0, 0, MidiController::Modulation, 1, 20, 0.006862745098),
                  midi::controller(12, 0, MidiController::Modulation, 2, 20, 0.015686274510),
              },
      }},
  };

  const auto usage = analyzePerformanceModulationUsage(performance);
  applyMidiModulationScaling(midiSequence, usage, ModulationScalingPolicy::ObservedSequenceRange);

  const auto& events = midiSequence.tracks[0].events;
  expect(midiController(events[0], MidiController::Modulation)->value == 56,
         "observed modulation scaling should use precise source amounts instead of rescaling rounded 7-bit values");
  expect(midiController(events[1], MidiController::Modulation)->value == 127,
         "observed modulation scaling should expand the precise observed maximum to full controller range");
}

}  // namespace

void runValueMidiTests() {
  midiExporterWritesStandardMidiFile();
  midiExporterKeeps14BitControllerPairsAdjacent();
  midiExporterWritesAllSoundOffImmediatelyBeforeNoteOn();
  midiExporterPreservesLegacyPortamentoTimeByteOrder();
  midiExporterOrdersFineTuneBeforeSameTickProgramChange();
  midiExporterKeepsSameTickBankProgramPairsAdjacent();
  midiExporterWritesTimeSignatureMetaEvent();
  midiExporterOrdersGeneratedNoteOffBeforeSameTickNoteOn();
  midiExporterKeepsZeroDurationNotePairedAtSameTick();
  performanceMidiRendererTrustsSourceNoteExtensions();
  performanceMidiRendererSelectsWideTuningRepresentation();
  performanceMidiRendererWritesTimeSignaturesToFirstTrack();
  performanceMidiRendererWritesPanGainResetWhenRequested();
  performanceMidiRendererCombinesExpressionWithPanGain();
  performanceMidiRendererLowersDeclaredPanLaws();
  performanceMidiRendererRetainsPanLawDuringLfoSimulation();
  performanceMidiRendererHonorsMidiExportOptions();
  performanceMidiRendererCanTerminatePreviousVoices();
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
  performanceMidiLoweringAppliesPitchResetsBeforeLaterTransitions();
  performanceMidiRendererLeavesTerminalPitchBentWithoutAnotherAttack();
  performanceMidiRendererCombinesSourceBendWithPitchTransitions();
  performanceMidiLoweringCanContinueAnAbsoluteCurveAcrossNewNotes();
  performanceMidiRendererResolvesSourceInstrumentIdentityAtExport();
  performanceMidiRendererQuantizesPitchBendAndPortamento();
  performanceMidiRendererSkipsRedundantPitchBends();
  performanceMidiRendererSimulatesDelayedVibratoAsPitchBendShape();
  performanceMidiRendererHonorsSpecifiedLfoWaveform();
  performanceMidiRendererHonorsSteppedLfoSamplesAndHeldDisableValue();
  performanceMidiRendererReplacesSampledLfoWithNamedWaveform();
  performanceMidiRendererDoesNotDoubleDelayVibrato();
  performanceMidiRendererRestartsSimulatedVibratoDelayForNewNotes();
  performanceMidiRendererReplacesSavedNoteDelay();
  performanceMidiRendererSimulatesTremoloUsingGlobalTempo();
  performanceMidiRendererHonorsNoBoostTremoloPhaseAndResetPolicy();
  exportRequestSequenceLoopsAffectMidiLowering();
  standaloneSequenceExportDoesNotRequireACollection();
  modulationAnalysisReportsObservedPerformanceMaxima();
  physicalModulationProfileDrivesMidiAndSynthFromOnePlan();
  tempoRelativeModulationFollowsTheGlobalTempoTimeline();
  observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators();
  observedModulationScalingUsesPreciseNormalizedAmounts();
}

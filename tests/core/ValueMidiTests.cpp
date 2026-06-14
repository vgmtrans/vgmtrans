/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

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

void performanceMidiRendererExtendsPreviousSameKeyNotes() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 2,
          .endTick = 24,
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
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 18},
                      .key = 62.0,
                      .linearVelocity = 0.5,
                      .durationTicks = 6,
                      .extendsPrevious = true,
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
  expect(firstNote != nullptr && firstNote->tick == 0 && firstNote->key == 60 && firstNote->duration == 18,
         "performance renderer should extend a previous same-key note");
  expect(secondNote != nullptr && secondNote->tick == 18 && secondNote->key == 62 && secondNote->duration == 6,
         "performance renderer should emit a new note when no matching previous key exists");
  expect(std::get<EndOfTrack>(events.back()).tick == 24, "performance renderer should preserve track end ticks");
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


}  // namespace

void runValueMidiTests() {
  midiExporterWritesStandardMidiFile();
  performanceMidiRendererExtendsPreviousSameKeyNotes();
  performanceMidiRendererHonorsMidiExportOptions();
  modulationAnalysisReportsObservedMidiControllerRanges();
  modulationAnalysisReportsObservedPerformanceRanges();
  observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators();
}

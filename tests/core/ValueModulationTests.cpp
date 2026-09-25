/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"

#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/TempoRelativeModulation.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace vgmtrans::core;

namespace {

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
  expect(usage.vibratoDepth == 82.0 / 127.0,
         "performance modulation analysis should report global vibrato depth maximum");
  expect(usage.vibratoRate == 29.0 / 127.0,
         "performance modulation analysis should report global vibrato rate maximum");
  expect(usage.tremoloDepth == 40.0 / 127.0,
         "performance modulation analysis should report global tremolo depth maximum");
  expect(usage.tremoloRate == 9.0 / 127.0, "performance modulation analysis should report global tremolo rate maximum");
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
  PerformanceEmitter out{track,         {track.id, CommandId{0}}, SourceAnnotationId{0}, 0, nextSequence, nextNote,
                         nextAutomation};

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

  expect(profile.instruments.vibrato && profile.instruments.tremolo,
         "physical LFO authoring should produce one shared sequence plan");
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
          .amount = 0.9,
          .context = LfoPerformanceContext{.shape = LfoShape{.waveform = LfoWaveform::Square}},
      }},
  };
  expect(analyzeSequenceModulation(PerformanceSequence{.tracks = {ordinaryTrack}}).empty(),
         "normalized-only modulation should not create a physical sequence plan");
  const auto combinedProfile = analyzeSequenceModulation(PerformanceSequence{.tracks = {track, ordinaryTrack}});
  expect(
      combinedProfile.instruments.vibrato && combinedProfile.instruments.vibrato->waveform == LfoWaveform::SawtoothUp,
      "normalized-only modulation should not alter a physical waveform profile");
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
                  .events =
                      {
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 0},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .context = LfoPerformanceContext{.cyclesPerTick = 0.25},
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.track = TrackId{0}, .tick = 0, .sequence = 1},
                              .target = ModulationPerformanceTarget::VibratoDelay,
                              .context = {.delay = LfoDelay{.ticks = 10, .tempoRelative = true}}},
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
  std::vector<const ModulationPerformanceEvent*> delays;
  for (const PerformanceEvent& event : performance.tracks[0].events) {
    if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
      (modulation->target == ModulationPerformanceTarget::VibratoDelay ? delays : rates).push_back(modulation);
    }
  }

  expect(rates.size() == 3 && rates[0]->header.tick == 0 && rates[0]->context.frequencyHz &&
             std::abs(*rates[0]->context.frequencyHz - 25.0) < 0.0001 && rates[1]->header.tick == 10 &&
             rates[1]->context.frequencyHz && std::abs(*rates[1]->context.frequencyHz - 50.0) < 0.0001 &&
             rates[1]->header.sequence == 2 && rates[2]->header.tick == 20 && rates[2]->context.frequencyHz &&
             std::abs(*rates[2]->context.frequencyHz - 7.0) < 0.0001,
         "tempo-relative LFO rates should follow cross-track tempo changes in global execution order");
  expect(
      delays.size() == 3 &&
          std::ranges::all_of(
              delays, [](const auto* event) { return event->context.delay && event->context.delay->tempoRelative; }) &&
          delays[0]->context.delay->milliseconds &&
          std::abs(*delays[0]->context.delay->milliseconds - 100.0) < 0.0001 && delays[1]->header.tick == 10 &&
          delays[1]->context.delay->milliseconds && std::abs(*delays[1]->context.delay->milliseconds - 50.0) < 0.0001 &&
          delays[2]->header.tick == 30 && delays[2]->context.delay->milliseconds &&
          std::abs(*delays[2]->context.delay->milliseconds - 25.0) < 0.0001,
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

void tempoRelativeModulationKeepsIndependentRatesAndDelayPolicies() {
  PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .initialTempoMicrosecondsPerQuarter = 1'000'000,
      .tracks = {{.id = TrackId{0}, .endTick = 40}, {.id = TrackId{1}, .endTick = 40}},
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{
      performance.tracks[0], {performance.tracks[0].id, CommandId{7}}, SourceAnnotationId{8}, 0, nextSequence, nextNote,
      nextAutomation};
  out.vibratoRateCyclesPerTick(0.25, {}, PitchBendLayerId{2});
  out.vibratoRateCyclesPerTick(0.125, {}, PitchBendLayerId{0});
  out.panLfoRateCyclesPerTick(0.5);
  out.tremoloRateCyclesPerTick(0.375);
  out.vibratoDelay(LfoDelay{.ticks = 4, .tempoRelative = true, .updateMode = LfoDelayUpdateMode::FutureNotesOnly});
  out.tremoloDelay(LfoDelay{.ticks = 8, .tempoRelative = true, .updateMode = LfoDelayUpdateMode::FutureNotesOnly});
  out.at(20).vibratoRate(7.0, {}, PitchBendLayerId{2});
  out.at(20).tremoloRate(9.0);
  out.at(20).vibratoDelay(LfoDelay{.milliseconds = 3.0});
  PerformanceEmitter tempo{performance.tracks[1],  {performance.tracks[1].id, CommandId{9}},
                           SourceAnnotationId{10}, 0,
                           nextSequence,           nextNote,
                           nextAutomation};
  tempo.at(10).tempo(500'000);
  tempo.at(30).tempo(250'000);

  resolveTempoRelativeModulation(performance);
  const auto at = [&](u64 tick) {
    std::vector<const PerformanceEvent*> events;
    for (const auto& event : performance.tracks[0].events) {
      if (performanceEventHeader(event).tick == tick) {
        events.push_back(&event);
      }
    }
    return events;
  };
  const auto first = at(10);
  const auto second = at(30);
  expect(first.size() == 6 && second.size() == 3,
         "fixed-clock replacements must remove only their own rate or delay from future tempo updates");
  for (size_t i = 0; i < 4; ++i) {
    const auto& rate = std::get<ModulationPerformanceEvent>(*first[i]);
    expect(rate.context.frequencyHz == (i + 1) * 25.0 && rate.header.sourceAnnotation == SourceAnnotationId{10} &&
               !rate.header.sourceCommand.valid() && rate.header.track == TrackId{0},
           "derived rates must retain target/layer order and attribute the update to the tempo's source");
  }
  expect(std::get<ModulationPerformanceEvent>(*first[0]).pitchLayer == PitchBendLayerId{0} &&
             std::get<ModulationPerformanceEvent>(*first[1]).pitchLayer == PitchBendLayerId{2} &&
             std::get<ModulationPerformanceEvent>(*second[0]).context.frequencyHz == 50.0 &&
             std::get<ModulationPerformanceEvent>(*second[1]).context.frequencyHz == 200.0,
         "independent vibrato layers and pan must survive another layer's fixed-clock replacement");
  const auto& vibrato = std::get<ModulationPerformanceEvent>(*first[4]);
  const auto& tremolo = std::get<ModulationPerformanceEvent>(*second[2]);
  expect(vibrato.target == ModulationPerformanceTarget::VibratoDelay && vibrato.context.delay &&
             tremolo.target == ModulationPerformanceTarget::TremoloDelay && tremolo.context.delay &&
             vibrato.context.delay->milliseconds == 20.0 && tremolo.context.delay->milliseconds == 20.0 &&
             vibrato.context.delay->updateMode == LfoDelayUpdateMode::FutureNotesOnly &&
             tremolo.context.delay->updateMode == LfoDelayUpdateMode::FutureNotesOnly,
         "tempo updates must preserve both delay policies while resolving their physical duration");
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
  applyMidiModulationScaling(midiSequence, usage, ModulationScalingPolicy::FullFormatRange);
  expect(midiController(midiSequence.tracks[0].events[1], MidiController::Modulation)->value == 41,
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
      .source = SynthSource::ChannelPressure,
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

void observedModulationScalingPreservesQuantizationBoundaries() {
  struct Case {
    std::optional<double> maximum;
    s32 quantized;
    s32 precise;
    s32 synth;
  };
  for (const Case test :
       {Case{std::nullopt, 63, 63, 1000}, Case{0.0, 0, 0, 0}, Case{1e-12, 0, 64, 0}, Case{0.5 / 127.0, 127, 64, 4},
        Case{126.49 / 127.0, 64, 64, 996}, Case{126.5 / 127.0, 63, 63, 1000}}) {
    const MidiModulationUsage usage{.vibratoDepth = test.maximum};
    MidiSequence sequence{.tracks = {MidiTrack{.events = {
                                                   midi::controller(0, 0, MidiController::Modulation, 63),
                                                   midi::controller(1, 0, MidiController::Modulation, 63, 20,
                                                                    test.maximum.value_or(0.0) / 2.0),
                                               }}}};
    applyMidiModulationScaling(sequence, usage, ModulationScalingPolicy::ObservedSequenceRange);
    expect(
        midiController(sequence.tracks[0].events[0], MidiController::Modulation)->value == test.quantized &&
            midiController(sequence.tracks[0].events[1], MidiController::Modulation)->value == test.precise,
        "scaling must distinguish unobserved, zero, sub-byte, and full-range maxima while retaining source precision");
    expect(scaledSynthModulatorAmount(SynthModulator{.destination = SynthDestination::VibratoDepth, .amount = 1000},
                                      &usage, ModulationScalingPolicy::ObservedSequenceRange) == test.synth,
           "synth scaling must use the precise maximum and the same quantized headroom decision as MIDI");
  }
}

}  // namespace

void runValueModulationTests() {
  modulationAnalysisReportsObservedPerformanceMaxima();
  physicalModulationProfileDrivesMidiAndSynthFromOnePlan();
  tempoRelativeModulationFollowsTheGlobalTempoTimeline();
  tempoRelativeModulationKeepsIndependentRatesAndDelayPolicies();
  observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators();
  observedModulationScalingUsesPreciseNormalizedAmounts();
  observedModulationScalingPreservesQuantizationBoundaries();
}

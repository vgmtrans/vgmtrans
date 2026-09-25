/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace vgmtrans::core;

namespace {

void performanceMidiRendererChoosesPitchTransitionRepresentationAtLowering() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote,
                         nextAutomation};
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
  PerformanceEmitter rateOut{
      rateTrack, {rateTrack.id, CommandId{3}}, SourceAnnotationId{4}, 0, rateSequence, rateNote, rateAutomation};
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
  PerformanceEmitter fixedOut{
      fixedTrack, {fixedTrack.id, CommandId{5}}, SourceAnnotationId{6}, 0, fixedSequence, fixedNote, fixedAutomation};
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
  PerformanceEmitter out{track,         {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote,
                         nextAutomation};
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
         "pitch-bend continuation should retain the voice started by MIDI portamento");

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
  PerformanceEmitter out{track,         {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote,
                         nextAutomation};
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
}

void performanceMidiRendererHonorsRequiredPortamento() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{3}}, SourceAnnotationId{4}, 0, nextSequence, nextNote,
                         nextAutomation};
  out.portamentoEnable(true);
  out.note(60, 1.0, 8);
  const PerformanceNoteId destination = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(destination, 60, 64, 4).requirePortamento();

  const MidiSequence midi = renderMidiSequence(
      PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .tracks = {track},
      },
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const auto& events = midi.tracks.front().events;
  const auto notes = midiNotes(events);
  expect(notes.size() == 2 && notes[0].tick == 0 && notes[0].key == 60 && notes[1].tick == 4 && notes[1].key == 64,
         "required portamento should retain both source attacks");
  expect(
      std::ranges::any_of(
          events, [](const MidiEvent& event) { return isMidiController(event, MidiController::PortamentoControl); }) &&
          std::ranges::none_of(
              events,
              [](const MidiEvent& event) { return isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend); }),
      "required portamento should reject a channel-wide pitch-bend override");

  const MidiSequence terminatingPortamento = renderMidiSequence(
      PerformanceSequence{.timebase = Timebase{.ppqn = 48}, .tracks = {track}},
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento, .terminatePreviousVoice = true});
  expect(std::ranges::count_if(
             terminatingPortamento.tracks[0].events,
             [](const MidiEvent& event) { return isMidiController(event, MidiController::AllSoundOff); }) == 1,
         "new-attack termination should still cut off an unlinked portamento destination");
}

void performanceMidiRendererStartsANewVoiceAfterPitchBendContinuationWhenMidiPortamentoTakesOver() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{5}}, SourceAnnotationId{6}, 0, nextSequence, nextNote,
                         nextAutomation};
  const PerformanceNoteId first = out.note(60, 1.0, 4);
  const PerformanceNoteId second = out.at(4).note(64, 1.0, 4);
  out.at(4).pitchSlide(second, 60, 64, 3).continueFrom(first).preferPitchBend();
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
             notes[1].tick == 8 && notes[1].key == 67 &&
             std::ranges::none_of(midi.tracks[0].events,
                                  [](const MidiEvent& event) {
                                    const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                                    return event.tick == 8 && bend != nullptr && std::abs(bend->value) > 1024;
                                  }),
         "MIDI portamento should start its new voice without exposing the held bend under its smaller range");
}

void performanceMidiRendererResetsHeldPitchBeforeMidiPortamentoTakesOver() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 12,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{7}}, SourceAnnotationId{8}, 0, nextSequence, nextNote,
                         nextAutomation};
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
         "MIDI portamento replacing a held pitch bend should receive an untransposed destination note");
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
  PerformanceEmitter out{track,         {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, nextSequence, nextNote,
                         nextAutomation};
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

void performanceMidiRendererAddsIndependentPitchLfosWithoutRestartingChannelPhase() {
  constexpr PitchBendLayerId channelLayer{1};
  constexpr PitchBendLayerId voiceLayer{2};
  const auto context = [](double phase, bool restartsOnNote) {
    return LfoPerformanceContext{
        .cyclesPerTick = 0.25,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = phase,
        .noteRestartInitialPhaseCycles = phase,
        .restartMode = LfoRestartMode::None,
        .restartsOnNote = restartsOnNote,
    };
  };
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 6,
          .events =
              {
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .pitchLayer = channelLayer,
                      .context = context(0.5, false),
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 1},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .pitchLayer = channelLayer,
                      .pitchDepthSemitones = 1.0,
                      .context = context(0.5, false),
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 2},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .pitchLayer = voiceLayer,
                      .context = context(0.0, true),
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 3},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .pitchLayer = voiceLayer,
                      .pitchDepthSemitones = 0.5,
                      .context = context(0.0, true),
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0, .sequence = 4},
                      .key = 60,
                      .durationTicks = 2,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 2, .sequence = 5},
                      .key = 62,
                      .durationTicks = 4,
                  },
              },
      }},
  };
  const MidiSequence midi = renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators);
  const auto lastPitchBendAt = [&](u64 tick) -> std::optional<s16> {
    std::optional<s16> result;
    for (const auto& event : midi.tracks.front().events) {
      if (const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
          bend != nullptr && event.tick == tick) {
        result = bend->value;
      }
    }
    return result;
  };
  expect(lastPitchBendAt(2) == -4096,
         "a fresh note should restart only the voice LFO while preserving the additive channel LFO phase");
  const auto combined = lastPitchBendAt(4);
  expect(combined == 6144,
         "independent pitch LFO layers should add before conversion to the shared MIDI pitch wheel (observed " +
             (combined ? std::to_string(*combined) : std::string("none")) + ")");
}

void performanceMidiRendererSimulatesDeterministicSampleAndHoldNoise() {
  constexpr PitchBendLayerId noiseLayer{2};
  const auto context = LfoPerformanceContext{
      .cyclesPerTick = 0.5,
      .shape = LfoShape{.waveform = LfoWaveform::Noise},
      .initialPhaseCycles = 0.0,
      .restartMode = LfoRestartMode::None,
      .restartsOnNote = true,
      .phaseRunsAtZeroDepth = true,
  };
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .endTick = 4,
          .events =
              {
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.sequence = 0},
                      .target = ModulationPerformanceTarget::VibratoRate,
                      .pitchLayer = noiseLayer,
                      .context = context,
                  },
                  ModulationPerformanceEvent{
                      .header = PerformanceEventHeader{.sequence = 1},
                      .target = ModulationPerformanceTarget::VibratoDepth,
                      .pitchLayer = noiseLayer,
                      .pitchDepthSemitones = 3.0,
                      .context = context,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.sequence = 2},
                      .key = 60,
                      .durationTicks = 4,
                  },
              },
      }},
  };
  const MidiSequence midi = renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators);
  const auto nonzero = std::ranges::find_if(midi.tracks.front().events, [](const MidiEvent& event) {
    const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
    return bend != nullptr && bend->value != 0;
  });
  const auto ranges = midiPitchBendRanges(midi.tracks.front().events);
  expect(nonzero != midi.tracks.front().events.end() && nonzero->tick == 3 &&
             ranges == std::vector<std::pair<u64, u16>>{{0, 200}, {0, 300}},
         "noise modulation should reserve its range, hold zero through the first cycle, then hold a reproducible sample"
         " (ranges=" +
             std::to_string(ranges.size()) +
             ", first=" + (ranges.empty() ? std::string("none") : std::to_string(ranges.front().second)) + ")");
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
  PerformanceEmitter out{track,         {track.id, CommandId{9}}, SourceAnnotationId{10}, 0, nextSequence, nextNote,
                         nextAutomation};
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
  PerformanceEmitter out{track,         {track.id, CommandId{3}}, SourceAnnotationId{4}, 0, nextSequence, nextNote,
                         nextAutomation};
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

void performanceMidiRendererKeepsSampledPitchCurvesSparse() {
  PerformanceTrack track{
      .id = TrackId{0},
      .sourceTrackNumber = 0,
      .endTick = 8,
  };
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{4}}, SourceAnnotationId{5}, 0, nextSequence, nextNote,
                         nextAutomation};
  const PerformanceNoteId note = out.note(60, 1.0, 8);
  out.pitchSlide(note, 60, 62, 6).sample(out.at(4), 61);

  const MidiSequence midi = renderMidiSequence(PerformanceSequence{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .tracks = {track},
  });
  std::vector<u64> bendTicks;
  for (const MidiEvent& event : midi.tracks.front().events) {
    if (midiChannelMessage(event, MidiChannelMessageKind::PitchBend) != nullptr) {
      bendTicks.push_back(event.tick);
    }
  }
  expect(std::ranges::find(bendTicks, 4) != bendTicks.end() && std::ranges::find(bendTicks, 6) != bendTicks.end() &&
             std::ranges::none_of(bendTicks, [](u64 tick) { return tick == 1 || tick == 2 || tick == 3 || tick == 5; }),
         "sampled pitch curves should emit their actual changes without redundant per-tick bend writes");
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
  PerformanceEmitter out{track,         {track.id, CommandId{5}}, SourceAnnotationId{6}, 0, nextSequence, nextNote,
                         nextAutomation};
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
  PerformanceEmitter out{track,         {track.id, CommandId{7}}, SourceAnnotationId{8}, 0, nextSequence, nextNote,
                         nextAutomation};
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
  PerformanceEmitter out{track,         {track.id, CommandId{8}}, SourceAnnotationId{9}, 0, nextSequence, nextNote,
                         nextAutomation};
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
  PerformanceEmitter out{track,         {track.id, CommandId{9}}, SourceAnnotationId{10}, 0, nextSequence, nextNote,
                         nextAutomation};
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
  PerformanceEmitter out{track,         {track.id, CommandId{11}}, SourceAnnotationId{12}, 0, nextSequence, nextNote,
                         nextAutomation};
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
  const MidiSequence midi = renderMidiSequence(performance);
  const auto ranges = midiPitchBendRanges(midi.tracks[0].events);
  const auto hasRange = [&](u64 tick, u16 cents) {
    return std::ranges::find(ranges, std::pair{tick, cents}) != ranges.end();
  };
  const auto hasBend = [&](u64 tick, s16 value) {
    return std::ranges::any_of(midi.tracks[0].events, [&](const MidiEvent& event) {
      const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
      return bend != nullptr && event.tick == tick && bend->value == value;
    });
  };
  expect(hasBend(4, 6963) && hasBend(6, 6144) && hasBend(8, -341) && hasRange(0, 500) && !hasRange(6, 800) &&
             hasRange(8, 600),
         "a held transition should mask source range changes until the next physical attack");

  PerformanceTrack delayedTransitionTrack{
      .id = TrackId{1},
      .sourceTrackNumber = 1,
      .endTick = 12,
  };
  PerformanceEmitter delayedTransitionOut{delayedTransitionTrack, {delayedTransitionTrack.id, CommandId{13}},
                                          SourceAnnotationId{14}, 0,
                                          nextSequence,           nextNote,
                                          nextAutomation};
  delayedTransitionOut.pitchBendRange(2);
  delayedTransitionOut.note(60, 1.0, 4);
  constexpr PitchBendLayerId modulationLayer{1};
  delayedTransitionOut.at(2).pitchBend(0.25, modulationLayer);
  const PerformanceNoteId delayedTransitionNote = delayedTransitionOut.at(4).note(62, 1.0, 8);
  delayedTransitionOut.at(6).pitchSlide(delayedTransitionNote, 62, 72, 2);
  delayedTransitionOut.at(7).pitchBend(0.5, modulationLayer);
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
      return bend != nullptr && event.tick == 4 && bend->value == 186;
    });
    const auto combinedBend = std::ranges::find_if(delayedTransitionMidi.tracks[0].events, [](const MidiEvent& event) {
      const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
      return bend != nullptr && event.tick == 7 && bend->value == 4096;
    });
    const auto bendsAtTransitionTick =
        std::ranges::count_if(delayedTransitionMidi.tracks[0].events, [](const MidiEvent& event) {
          return event.tick == 7 && isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend);
        });
    const auto delayedTransitionRanges = midiPitchBendRanges(delayedTransitionMidi.tracks[0].events);
    expect(std::ranges::find(delayedTransitionRanges, std::pair<u64, u16>{4, 1100}) != delayedTransitionRanges.end() &&
               preservedBend != delayedTransitionMidi.tracks[0].events.end() &&
               combinedBend != delayedTransitionMidi.tracks[0].events.end() && bendsAtTransitionTick == 1,
           "independent modulation should combine with a slide and its new voice range");
  }

  PerformanceTrack sameVoiceTrack{
      .id = TrackId{1},
      .sourceTrackNumber = 1,
      .endTick = 12,
  };
  PerformanceEmitter sameVoiceOut{
      sameVoiceTrack, {sameVoiceTrack.id, CommandId{13}}, SourceAnnotationId{14}, 0, nextSequence, nextNote,
      nextAutomation};
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

void performanceMidiRendererExpandsRangeForComposedPitchLayers() {
  PerformanceTrack track{.id = TrackId{0}, .endTick = 16};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{15}}, SourceAnnotationId{16}, 0, nextSequence, nextNote,
                         nextAutomation};
  constexpr PitchBendLayerId secondLayer{1};

  out.note(60, 1.0, 8);
  out.at(1).pitchBend(2.0);
  out.at(2).pitchBend(2.0, secondLayer);
  out.at(8).pitchBend(0.0);
  out.at(8).pitchBend(0.0, secondLayer);
  out.at(8).instrument(0, 1);
  out.at(9).note(60, 1.0, 7);
  PitchBendPerformanceEvent normalizedBend{.semitones = 1.5, .normalizedWheelPosition = 0.75};
  out.at(10).pitchBend(normalizedBend);
  normalizedBend.layer = secondLayer;
  out.at(11).pitchBend(normalizedBend);
  out.at(12).instrument(0, 2);

  const PerformanceSequence performance{.timebase = Timebase{.ppqn = 48}, .tracks = {track}};
  const SoundBankAsset soundBank{
      .instruments = {
          Instrument{.explicitAddress = InstrumentAddress{.bank = 0, .program = 1}, .pitchBendRangeCents = 400},
          Instrument{.explicitAddress = InstrumentAddress{.bank = 0, .program = 2}, .pitchBendRangeCents = 200},
      }};
  const std::array<const SoundBankAsset*, 1> soundBanks{&soundBank};
  const MidiSequence midi =
      renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);
  const auto& events = midi.tracks[0].events;
  const auto updatedBend = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
    return bend != nullptr && event.tick == 12 && bend->value == 4096;
  });
  expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, 400}, {9, 600}} &&
             updatedBend != events.end(),
         "composed layers should expand the voice range and follow normalized-wheel range changes");
}

void performanceMidiRendererResolvesNormalizedWheelBeforeLoweringTransitions() {
  PerformanceTrack track{.id = TrackId{0}, .endTick = 8};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{17}}, SourceAnnotationId{18}, 0, nextSequence, nextNote,
                         nextAutomation};

  const PerformanceNoteId first = out.note(NotePerformanceEvent{
      .key = 60,
      .linearVelocity = 1.0,
      .durationTicks = 4,
      .instrumentAddress = InstrumentAddress{.bank = 0, .program = 1},
  });
  out.pitchBend(PitchBendPerformanceEvent{.semitones = 1.0, .normalizedWheelPosition = 0.5});
  const PerformanceNoteId second = out.at(4).note(62, 1.0, 4);
  out.at(4).pitchSlide(second, 62, 65, 2).continueFrom(first);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .tracks = {track},
  };
  const SoundBankAsset soundBank{
      .instruments = {
          Instrument{.explicitAddress = InstrumentAddress{.bank = 0, .program = 1}, .pitchBendRangeCents = 400},
      }};
  const std::array<const SoundBankAsset*, 1> soundBanks{&soundBank};
  const PerformanceTempoMap tempos{performance};

  const PerformanceSequence pitchBend = lowerMidiPerformanceAutomation(performance, {}, tempos, soundBanks);
  const auto heldStart = std::ranges::find_if(pitchBend.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 4 && bend->layer != kPrimaryPitchBendLayer;
  });
  const PerformanceSequence portamento = lowerMidiPerformanceAutomation(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento}, tempos, soundBanks);
  const auto sourceReset = std::ranges::find_if(portamento.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 4 && bend->layer == kPrimaryPitchBendLayer &&
           bend->semitones == 0.0 && !bend->normalizedWheelPosition;
  });
  const MidiSequence midi =
      renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);

  expect(heldStart != pitchBend.tracks[0].events.end() &&
             std::get<PitchBendPerformanceEvent>(*heldStart).semitones == 0.0 &&
             sourceReset != portamento.tracks[0].events.end() &&
             midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 500}},
         "transition lowering and range planning should share the selected instrument's normalized-wheel pitch");
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
  PerformanceEmitter out{track,         {track.id, CommandId{7}}, SourceAnnotationId{8}, 0, nextSequence, nextNote,
                         nextAutomation};
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
                      .instrument = InstrumentIdentity{.domain = "probe.instrument", .key = 5},
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
                  PitchTransitionSettingsPerformanceEvent{
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
                               return time != nullptr && event.tick == 12 && time->value == 0;
                             }),
         "MIDI renderer should write the high seven bits of the physical portamento time");
  expect(firstMidiController14(events, MidiController::PortamentoTime) == 83,
         "MIDI renderer should retain the full physical portamento time");
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

}  // namespace

void runValueMidiPitchTests() {
  performanceMidiRendererChoosesPitchTransitionRepresentationAtLowering();
  performanceMidiRendererAllowsMixedPitchTransitionRendering();
  performanceMidiRendererRetainsHeldVoiceAcrossChainedPitchBends();
  performanceMidiRendererHonorsRequiredPortamento();
  performanceMidiRendererStartsANewVoiceAfterPitchBendContinuationWhenMidiPortamentoTakesOver();
  performanceMidiRendererResetsHeldPitchBeforeMidiPortamentoTakesOver();
  performanceMidiRendererCombinesPitchSlidesWithSimulatedVibrato();
  performanceMidiRendererAddsIndependentPitchLfosWithoutRestartingChannelPhase();
  performanceMidiRendererSimulatesDeterministicSampleAndHoldNoise();
  performanceMidiRendererUsesOnlyFrozenVibratoOffsetForPitchRange();
  performanceMidiRendererUsesWholeSemitonePitchBendRanges();
  performanceMidiRendererDoesNotRestartVibratoAtAHeldPitchSlideBoundary();
  performanceMidiRendererPreservesExactSamplesAndChainedPitchContinuity();
  performanceMidiRendererKeepsSampledPitchCurvesSparse();
  performanceMidiRendererResetsInterruptedPitchBeforeTheNewNote();
  performanceMidiRendererDefersPitchResetUntilTheNextAttack();
  performanceMidiLoweringAppliesPitchResetsBeforeLaterTransitions();
  performanceMidiRendererLeavesTerminalPitchBentWithoutAnotherAttack();
  performanceMidiRendererCombinesSourceBendWithPitchTransitions();
  performanceMidiRendererExpandsRangeForComposedPitchLayers();
  performanceMidiRendererResolvesNormalizedWheelBeforeLoweringTransitions();
  performanceMidiLoweringCanContinueAnAbsoluteCurveAcrossNewNotes();
  performanceMidiRendererResolvesSourceInstrumentIdentityAtExport();
  performanceMidiRendererQuantizesPitchBendAndPortamento();
  performanceMidiRendererSkipsRedundantPitchBends();
}

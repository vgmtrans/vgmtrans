/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"

#include "../PerformanceTestSupport.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace vgmtrans::core;

namespace {

void midiNotePlanningResolvesAttacksBeforeRendering() {
  PerformanceTrack track{.id = TrackId{0}, .endTick = 28};
  u64 order = 0;
  u32 noteId = 0, automationId = 0;
  PerformanceEmitter out{track, {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, order, noteId, automationId};
  const auto first = out.note(NotePerformanceEvent{
      .key = 60, .durationTicks = 4, .maximumDurationMilliseconds = 950.0});
  out.at(4).continueVoice(first, NotePerformanceEvent{
      .key = 60, .durationTicks = 4, .restartsVibratoLfoPhase = false, .restartsTremoloLfoPhase = true});
  out.at(5).note(NotePerformanceEvent{.key = 72, .durationTicks = 20, .lane = PerformanceLaneId{1}});
  const auto changed = out.at(8).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 4});
  const auto native = out.at(12).continueVoice(changed, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  out.at(12).pitchSlide(native, 64, 67, 4).preferPortamento().portamentoOverlap(2);
  const auto final = out.at(16).continueVoice(native, NotePerformanceEvent{.key = 69, .durationTicks = 8});
  out.at(20).continueVoice(final, NotePerformanceEvent{.key = 69, .durationTicks = 8});
  const auto prepared = preparePerformance({.timebase = {.ppqn = 10},
                                             .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
                                             .tracks = {track}});
  std::vector<Diagnostic> diagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
  std::vector<const detail::MidiNoteEvent*> notes;
  for (const auto& event : lowered) {
    if (const auto* note = std::get_if<detail::MidiNoteEvent>(&event)) notes.push_back(note);
  }
  using Action = detail::MidiNoteAction;
  expect(notes.size() == 7, "note planning must retain ordinary tie boundaries and native-portamento attacks");
  const std::array actions{Action::Attack, Action::Continue, Action::Attack, Action::Continue,
                           Action::Attack, Action::Continue, Action::Expired};
  const std::array<u64, 7> ticks{0, 4, 5, 8, 12, 16, 20};
  const std::array<double, 7> bases{60, 60, 72, 60, 67, 67, 67};
  for (size_t index = 0; index < notes.size(); ++index) {
    expect(notes[index]->action == actions[index] && notes[index]->header.tick == ticks[index] &&
               notes[index]->bendBaseKey == bases[index],
           "planned actions and bend references must follow their own voice through mixed pitch representations");
  }
  expect(notes[0]->endTick == 14 && notes[2]->endTick == 25 && notes[4]->endTick == 19,
         "the completed plan must include overlap and all extensions, clamp the voice deadline, and leave other voices alone");
  expect(notes[1]->restartsVibratoLfoPhase == false && notes[1]->restartsTremoloLfoPhase == true &&
             notes[1]->header.sourceAnnotation == SourceAnnotationId{2},
         "planning a tie must retain independent LFO reset decisions and source association");
  expect(std::get<NotePerformanceEvent>(prepared.performance().tracks[0].events.front()).durationTicks == 4,
         "the completed physical duration must not overwrite the prepared source gate");
  const auto midi = renderMidiSequence(prepared);
  const auto attacks = midiNotes(midi.tracks[0].events);
  expect(attacks.size() == 3 && attacks[0].tick == 0 && attacks[0].duration == 14 &&
             attacks[1].tick == 5 && attacks[1].duration == 20 &&
             attacks[2].tick == 12 && attacks[2].key == 67 && attacks[2].duration == 7,
         "MIDI encoding must emit the planned attacks and completed durations without further tie resolution");
}

void midiNotePlanningKeepsZeroDurationAttacksAndOrphanTies() {
  PerformanceSequence source{.timebase = {.ppqn = 10}, .tracks = {{.id = TrackId{0}, .events = {
      NotePerformanceEvent{.key = 60, .durationTicks = 4, .maximumDurationMilliseconds = 0.0},
      NotePerformanceEvent{.header = {.tick = 2}, .key = 67, .durationTicks = 4, .extendsPrevious = true},
  }}}};
  const auto prepared = preparePerformance(source);
  std::vector<Diagnostic> diagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
  const auto& zero = std::get<detail::MidiNoteEvent>(lowered[0]);
  const auto& orphan = std::get<detail::MidiNoteEvent>(lowered[1]);
  expect(zero.action == detail::MidiNoteAction::Attack && zero.endTick == 0 &&
             orphan.action == detail::MidiNoteAction::Attack && orphan.endTick == 6 && orphan.extendsPrevious,
         "a zero-duration fresh attack and a tie with no preceding physical note must each retain their Note On");
  const auto midi = renderMidiSequence(prepared);
  const auto attacks = midiNotes(midi.tracks[0].events);
  expect(attacks.size() == 2 && attacks[0].duration == 0 && attacks[1].key == 67 && attacks[1].duration == 4,
         "the renderer must distinguish the planned attack from the source's continuation request");
}

void midiNotePlanningDoesNotWrapLongExtensions() {
  const u32 maximum = std::numeric_limits<u32>::max();
  const PerformanceSequence source{.tracks = {{.id = TrackId{0}, .events = {
      NotePerformanceEvent{.key = 60, .durationTicks = maximum - 2, .note = PerformanceNoteId{0},
                           .voice = PerformanceVoiceId{0}},
      NotePerformanceEvent{.header = {.tick = maximum - 2}, .key = 60, .durationTicks = 10,
                           .extendsPrevious = true, .note = PerformanceNoteId{0}, .voice = PerformanceVoiceId{0}},
  }}}};
  const auto midi = renderTestMidi(source);
  const auto attacks = midiNotes(midi.tracks[0].events);
  expect(attacks.size() == 1 && attacks[0].duration == maximum,
         "a physical gate exceeding the MIDI model's duration field must saturate instead of wrapping to a short note");
}

void midiNotePlanningKeepsReleasePitchPastExpiredContinuations() {
  PerformanceTrack track{.id = TrackId{0}, .endTick = 14};
  u64 order = 0;
  u32 noteId = 0, automationId = 0;
  PerformanceEmitter out{track, {track.id, CommandId{1}}, SourceAnnotationId{2}, 0, order, noteId, automationId};
  const auto first = out.note(NotePerformanceEvent{
      .key = 60, .durationTicks = 4, .maximumDurationMilliseconds = 100.0});
  out.pitchSlide(first, 60, 64, 4).preferPitchBend();
  const auto expired = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  out.at(4).pitchSlide(expired, 64, 67, 4).preferPortamento();
  out.at(10).note(72, 1.0, 4);
  const auto prepared = preparePerformance({.timebase = {.ppqn = 10}, .tracks = {track}});
  std::vector<Diagnostic> diagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
  std::vector<std::pair<u64, double>> bends;
  for (const auto& event : lowered) {
    if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event)) {
      bends.emplace_back(bend->header.tick, bend->semitones);
    }
  }
  expect(bends == std::vector<std::pair<u64, double>>{{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {10, 0}},
         "an expired native-portamento continuation must not reset the preceding curve before a real attack");
  const auto midi = renderMidiSequence(prepared);
  const auto attacks = midiNotes(midi.tracks[0].events);
  expect(attacks.size() == 2 && attacks[0].duration == 2 && attacks[1].tick == 10,
         "a continuation after the hardware deadline must have no physical attack");
  expect(midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 200}, {0, 400}, {10, 200}},
         "an expired boundary must not start a new pitch-range interval");
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
  std::get<NotePerformanceEvent>(*sourceAttack).instrument = InstrumentAddress{.bank = 3, .program = 4};

  const MidiSequence native = renderTestMidi(
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
      renderTestMidi(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const auto bendLoweringInput = preparePerformance(performance);
  std::vector<Diagnostic> bendLoweringDiagnostics;
  const auto bendLowering = detail::lowerMidiTrackEvents(
      bendLoweringInput, 0, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend},
      PerformanceTempoMap{bendLoweringInput.performance()}, bendLoweringDiagnostics);
  const auto sourceNote = std::ranges::find_if(bendLoweringInput.performance().tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  const auto loweredNote = std::ranges::find_if(bendLowering, [](const auto& event) {
    return std::holds_alternative<detail::MidiNoteEvent>(event);
  });
  const auto notesMatch = [](const NotePerformanceEvent& lhs, const NotePerformanceEvent& rhs) {
    return lhs.header.sourceCommand == rhs.header.sourceCommand &&
           lhs.header.sourceAnnotation == rhs.header.sourceAnnotation && lhs.header.track == rhs.header.track &&
           lhs.header.tick == rhs.header.tick && lhs.header.sequence == rhs.header.sequence &&
           lhs.header.automation == rhs.header.automation && lhs.key == rhs.key &&
           lhs.linearVelocity == rhs.linearVelocity && lhs.durationTicks == rhs.durationTicks &&
           lhs.extendsPrevious == rhs.extendsPrevious && lhs.voice == rhs.voice && lhs.instrument == rhs.instrument &&
           lhs.restartsLfoPhase == rhs.restartsLfoPhase && lhs.restartsVibratoLfoPhase == rhs.restartsVibratoLfoPhase &&
           lhs.restartsTremoloLfoPhase == rhs.restartsTremoloLfoPhase && lhs.note == rhs.note && lhs.lane == rhs.lane;
  };
  expect(sourceNote != bendLoweringInput.performance().tracks[0].events.end() && loweredNote != bendLowering.end() &&
             notesMatch(std::get<NotePerformanceEvent>(*sourceNote), std::get<detail::MidiNoteEvent>(*loweredNote)),
         "pitch-bend lowering should preserve the prepared note segment verbatim");
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

void sourceVoiceKeyChangesNeedNoPitchBinding() {
  for (bool nativeGlide : {false, true}) {
    PerformanceTrack track{.id = TrackId{0}, .endTick = 16};
    u64 order = 0;
    u32 noteId = 0, automationId = 0;
    PerformanceEmitter out{track, {track.id, CommandId{1}}, SourceAnnotationId{1}, 0, order, noteId, automationId};
    const auto first = out.note(60, 1.0, 4);
    const auto second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 4});
    if (nativeGlide) {
      out.at(4).pitchSlide(second, 60, 64, 4).preferPortamento();
    }
    const auto third = out.at(8).continueVoice(second, NotePerformanceEvent{.key = 67, .durationTicks = 4});
    auto canceled = out.at(8).pitchSlide(third, 64, 67, 3);
    const auto fourth = out.at(12).continueVoice(third, NotePerformanceEvent{.key = 69, .durationTicks = 4});
    out.at(14).pitchSlide(fourth, 69, 70, 2).preferPitchBend();
    canceled.stop(out.at(8));
    const auto prepared = preparePerformance({.preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
                                               .tracks = {track}});
    std::vector<Diagnostic> diagnostics;
    const auto lowered = detail::lowerMidiTrackEvents(prepared, 0, {},
                                                       PerformanceTempoMap{prepared.performance()}, diagnostics);
    const auto heldBend = [&](u64 tick, double semitones) {
      return std::ranges::any_of(lowered, [&](const auto& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend && bend->header.tick == tick && bend->layer != kPrimaryPitchBendLayer &&
               std::abs(bend->semitones - semitones) < 0.000001;
      });
    };
    const double baseKey = nativeGlide ? 64.0 : 60.0;
    expect(heldBend(8, 67 - baseKey) && heldBend(12, 69 - baseKey),
           "key changes must use the held MIDI note's base, even after a canceled slide or native portamento");
    const auto midi = renderMidiSequence(prepared);
    const auto attacks = midiNotes(midi.tracks[0].events);
    expect(attacks.size() == (nativeGlide ? 2u : 1u) && attacks.back().duration == (nativeGlide ? 12u : 16u),
           "canceling or delaying pitch motion must not retrigger an explicitly continued voice");
    const auto native = renderMidiSequence(prepared, {.pitchTransitions = MidiPitchTransitionRendering::Portamento});
    expect(midiNotes(native.tracks[0].events).size() == 5 &&
               std::ranges::count_if(native.tracks[0].events, [](const MidiEvent& event) {
                 return isMidiController(event, MidiController::PortamentoControl);
               }) == 4,
           "explicit portamento policy must also apply to key changes without source pitch bindings");
  }
}

void sourceKeyChangeUsesRealizedPitchWithPortamento() {
  PerformanceTrack track{.id = TrackId{0}, .endTick = 8};
  u64 order = 0;
  u32 noteId = 0, automationId = 0;
  PerformanceEmitter out{track, {track.id, CommandId{1}}, SourceAnnotationId{1}, 0, order, noteId, automationId};
  const auto first = out.note(60, 1.0, 4);
  out.pitchSlide(first, 60, 64, 8).preferPitchBend();
  out.at(4).continueVoice(first, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  const auto prepared = preparePerformance({.tracks = {track}});
  std::vector<Diagnostic> diagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(prepared, 0, {},
                                                     PerformanceTempoMap{prepared.performance()}, diagnostics);
  expect(std::ranges::any_of(lowered, [](const auto& event) {
           const auto* glide = std::get_if<PortamentoPerformanceEvent>(&event);
           return glide && glide->header.tick == 4 && glide->previousKey == 62.0;
         }),
         "an implicit key change must start portamento from the interrupted glide's realized pitch");
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
  const PerformanceNoteId second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 4});
  out.at(4).pitchSlide(second, 60, 64, 4).preferPortamento();
  const PerformanceNoteId third = out.at(8).continueVoice(second, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  out.at(8).pitchSlide(third, 64, 67, 4).preferPitchBend();

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

  const MidiSequence preserved = renderTestMidi(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  expect(countPortamento(preserved) == 1 && countPitchBends(preserved) != 0,
         "PreserveFormat should allow portamento and pitch bend transitions in one track");
  expect(noteDuration(preserved, 0) == 5 && noteDuration(preserved, 4) == 8 && !noteDuration(preserved, 8),
         "pitch-bend continuation should retain the voice started by MIDI portamento");

  const MidiSequence allPortamento =
      renderTestMidi(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(countPortamento(allPortamento) == 2 && countPitchBends(allPortamento) == 0 &&
             noteDuration(allPortamento, 0) == 5 && noteDuration(allPortamento, 4) == 5 &&
             noteDuration(allPortamento, 8) == 4,
         "an explicit portamento request should override every transition preference");

  const MidiSequence terminatingPortamento = renderTestMidi(
      performance,
      MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento, .terminatePreviousVoice = true});
  expect(
      std::ranges::none_of(terminatingPortamento.tracks[0].events,
                           [](const MidiEvent& event) { return isMidiController(event, MidiController::AllSoundOff); }),
      "new-attack termination should not cut off linked native-portamento continuations");

  const MidiSequence allPitchBend =
      renderTestMidi(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
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
  const PerformanceNoteId second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 62, .durationTicks = 4});
  out.at(4).pitchSlide(second, 60, 62, 8);
  const PerformanceNoteId third = out.at(8).continueVoice(second, NotePerformanceEvent{.key = 64, .durationTicks = 4});
  out.at(8).pitchSlide(third, 62, 64, 4);
  const PerformanceNoteId fourth = out.at(12).continueVoice(third, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  out.at(12).pitchSlide(fourth, 64, 67, 4);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {track},
  };
  const MidiExportOptions bendOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend};
  const MidiSequence midi = renderTestMidi(performance, bendOptions);

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

  const MidiSequence midi = renderTestMidi(
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

  const MidiSequence terminatingPortamento = renderTestMidi(
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
  const PerformanceNoteId second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 4});
  out.at(4).pitchSlide(second, 60, 64, 3).preferPitchBend();
  const PerformanceNoteId third = out.at(8).continueVoice(second, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  out.at(8).pitchSlide(third, 64, 67, 4).preferPortamento();

  const MidiSequence midi = renderTestMidi(
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
  const PerformanceNoteId second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 8});
  out.at(4).pitchSlide(second, 60, 64, 4).preferPitchBend();
  out.at(8).pitchSlide(second, 64, 67, 4).preferPortamento();

  const MidiSequence midi = renderTestMidi(
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
  const MidiSequence synthModulators = renderTestMidi(performance, {}, ModulationConversionPolicy::SynthModulators);
  const MidiSequence simulated =
      renderTestMidi(performance, {}, ModulationConversionPolicy::SequenceEventSimulation);

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
  const MidiSequence midi = renderTestMidi(performance, {}, ModulationConversionPolicy::SynthModulators);
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
  const MidiSequence midi = renderTestMidi(performance, {}, ModulationConversionPolicy::SynthModulators);
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
      renderTestMidi(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
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

  const MidiSequence midi = renderTestMidi(performance);
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

void performanceMidiRendererPlansRangesAtPhysicalAttacks() {
  for (bool sharedOrder : {false, true}) {
    PerformanceTrack track{.id = TrackId{0}, .endTick = 10};
    u64 order = 0;
    u32 noteId = 0, automationId = 0;
    PerformanceEmitter out{track, {track.id, CommandId{1}}, SourceAnnotationId{1}, 0, order, noteId, automationId};
    const auto first = out.note(60, 1.0, 2);
    out.at(2).continueVoice(first, NotePerformanceEvent{.key = 60, .durationTicks = 4});
    out.at(3).pitchBend(6.0);
    out.at(6).pitchBend(PitchBendPerformanceEvent{.normalizedWheelPosition = 0.5});
    out.at(6).pitchBendRange(5);
    out.at(6).note(67, 1.0, 4);
    if (sharedOrder) {
      // Hand-built performances can give the controls and attack the same
      // tick/sequence. Their stable order still puts the controls first.
      for (auto& event : track.events) {
        std::visit([](auto& value) { value.header.sequence = 0; }, event);
      }
    }
    const auto midi = renderTestMidi(PerformanceSequence{.tracks = {track}});
    const auto& events = midi.tracks.front().events;
    const std::vector<std::pair<u64, u16>> expectedRanges =
        sharedOrder ? std::vector<std::pair<u64, u16>>{{0, 600}, {6, 200}, {6, 500}}
                    : std::vector<std::pair<u64, u16>>{{0, 600}, {6, 500}};
    expect(midiPitchBendRanges(events) == expectedRanges,
           "ties must reserve range at the original attack and a new attack must restore current source sensitivity");
    expect(std::ranges::any_of(events,
                               [](const MidiEvent& event) {
                                 const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                                 return event.tick == 6 && bend && bend->value == 4096;
                               }),
           "restoring sensitivity must preserve the normalized wheel's pitch at the new attack");
  }

  PerformanceTrack chord{.id = TrackId{0}, .endTick = 4};
  u64 order = 0;
  u32 noteId = 0, automationId = 0;
  PerformanceEmitter out{chord, {chord.id, CommandId{2}}, SourceAnnotationId{2}, 0, order, noteId, automationId};
  out.note(60, 1.0, 4);
  out.pitchBend(-6.0);
  out.note(67, 1.0, 4);
  out.at(1).pitchBend(2.0);
  const auto midi = renderTestMidi(PerformanceSequence{.tracks = {chord}});
  expect(midiPitchBendRanges(midi.tracks.front().events) == std::vector<std::pair<u64, u16>>{{0, 600}},
         "simultaneous MIDI attacks must share a range that includes pitch writes between their note events");
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
  const PerformanceNoteId second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 4});
  out.at(4).pitchSlide(second, 60, 64, 4);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 100},
      .tracks = {track},
  };
  const MidiExportOptions bendOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend};
  const MidiSequence plain = renderTestMidi(performance, bendOptions, ModulationConversionPolicy::SynthModulators);
  const MidiSequence simulated =
      renderTestMidi(performance, bendOptions, ModulationConversionPolicy::SequenceEventSimulation);
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
  const MidiSequence midi = renderTestMidi(performance);

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

  const MidiSequence midi = renderTestMidi(PerformanceSequence{
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

  const MidiSequence midi = renderTestMidi(PerformanceSequence{
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

  const auto loweredInput = preparePerformance(PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {track},
      });
  std::vector<Diagnostic> loweredDiagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(
      loweredInput, 0, {},
      PerformanceTempoMap{loweredInput.performance()}, loweredDiagnostics);
  std::vector<std::pair<u64, double>> bends;
  for (const auto& event : lowered) {
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
  const PerformanceNoteId heldTarget = out.at(10).continueVoice(heldStart, NotePerformanceEvent{.key = 70, .durationTicks = 20});
  out.at(10).pitchSlide(heldTarget, 68, 70, 5);
  out.at(16).pitchSlide(heldTarget, 70, 48, 4);

  const auto loweredInput = preparePerformance(PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {track},
      });
  std::vector<Diagnostic> loweredDiagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(
      loweredInput, 0, {},
      PerformanceTempoMap{loweredInput.performance()}, loweredDiagnostics);
  const auto bendAt = [&](u64 tick) -> std::optional<double> {
    std::optional<double> bend;
    for (const auto& event : lowered) {
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

  const auto loweredInput = preparePerformance(PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {track},
      });
  std::vector<Diagnostic> loweredDiagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(
      loweredInput, 0, {},
      PerformanceTempoMap{loweredInput.performance()}, loweredDiagnostics);
  const auto& events = lowered;
  const auto lastBend = std::find_if(events.rbegin(), events.rend(), [](const auto& event) {
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
  const PerformanceNoteId second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 64, .durationTicks = 4});
  out.at(4).pitchSlide(second, 60, 64, PitchSlideTiming::fromTicks(0));
  out.at(6).pitchBend(-0.25);
  out.at(6).pitchBendRange(8);
  out.at(8).note(67, 1.0, 4);
  out.at(8).pitchBendRange(6);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .tracks = {track},
  };
  const MidiSequence midi = renderTestMidi(performance);
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
    const MidiSequence delayedTransitionMidi = renderTestMidi(delayedTransitionPerformance, {}, policy);
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
  const auto sameVoiceLoweredInput = preparePerformance(PerformanceSequence{
          .timebase = Timebase{.ppqn = 48},
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
          .tracks = {sameVoiceTrack},
      });
  std::vector<Diagnostic> sameVoiceLoweredDiagnostics;
  const auto sameVoiceLowered = detail::lowerMidiTrackEvents(
      sameVoiceLoweredInput, 0, {},
      PerformanceTempoMap{sameVoiceLoweredInput.performance()}, sameVoiceLoweredDiagnostics);
  const auto sameVoiceStart =
      std::ranges::find_if(sameVoiceLowered, [](const auto& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend != nullptr && bend->header.tick == 4;
      });
  const auto sourceTakeover =
      std::ranges::find_if(sameVoiceLowered, [](const auto& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend != nullptr && bend->header.tick == 7 && bend->semitones == -1.0;
      });
  const auto resetAtNextAttack =
      std::ranges::find_if(sameVoiceLowered, [](const auto& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend != nullptr && bend->header.tick == 8 && bend->semitones == 0.0;
      });
  expect(sameVoiceStart != sameVoiceLowered.end() &&
             std::get<PitchBendPerformanceEvent>(*sameVoiceStart).semitones == 1.0 &&
             sourceTakeover != sameVoiceLowered.end() &&
             resetAtNextAttack == sameVoiceLowered.end(),
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
      renderTestMidi(performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);
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
      .instrument = InstrumentAddress{.bank = 0, .program = 1},
  });
  out.pitchBend(PitchBendPerformanceEvent{.semitones = 1.0, .normalizedWheelPosition = 0.5});
  const PerformanceNoteId second = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 62, .durationTicks = 4});
  out.at(4).pitchSlide(second, 62, 65, 2);

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

  const auto pitchBendInput = prepareTestPerformance(performance, soundBanks);
  std::vector<Diagnostic> pitchBendDiagnostics;
  const auto pitchBend = detail::lowerMidiTrackEvents(
      pitchBendInput, 0, {},
      tempos, pitchBendDiagnostics);
  const auto heldStart = std::ranges::find_if(pitchBend, [](const auto& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 4 && bend->layer != kPrimaryPitchBendLayer;
  });
  const auto portamentoInput = prepareTestPerformance(performance, soundBanks);
  std::vector<Diagnostic> portamentoDiagnostics;
  const auto portamento = detail::lowerMidiTrackEvents(
      portamentoInput, 0, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento},
      tempos, portamentoDiagnostics);
  const auto sourceReset = std::ranges::find_if(portamento, [](const auto& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 4 && bend->layer == kPrimaryPitchBendLayer &&
           bend->semitones == 0.0 && !bend->normalizedWheelPosition;
  });
  const MidiSequence midi =
      renderTestMidi(performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);

  expect(heldStart != pitchBend.end() &&
             std::get<PitchBendPerformanceEvent>(*heldStart).semitones == 0.0 &&
             sourceReset != portamento.end() &&
             midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 500}},
         "transition lowering and range planning should share the selected instrument's normalized-wheel pitch");
}

void performanceMidiLoweringCanContinueAnAbsoluteCurveAcrossNewNotes() {
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
  const PerformanceNoteId firstNote = out.note(64, 1.0, 4);
  out.pitchSlide(firstNote, 60, 68, 8).continueAcrossNotes();
  out.at(4).note(67, 1.0, 4);
  out.at(8).note(70, 1.0, 4);
  out.at(12).note(72, 1.0, 4);

  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .tracks = {track},
  };
  const auto loweredInput = preparePerformance(performance);
  std::vector<Diagnostic> loweredDiagnostics;
  const auto lowered = detail::lowerMidiTrackEvents(
      loweredInput, 0, {},
      PerformanceTempoMap{loweredInput.performance()}, loweredDiagnostics);
  const auto lastBendAt = [&](u64 tick) -> std::optional<double> {
    std::optional<double> value;
    for (const auto& event : lowered) {
      if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event); bend && bend->header.tick == tick) {
        value = bend->semitones;
      }
    }
    return value;
  };
  expect(performance.tracks[0].automations[0].realization.endTick == 8 && lastBendAt(4) == -3.0,
         "at a note boundary, the final bend must rebase the curve to the new note even when writes share an order");
  expect(lastBendAt(8) == -2.0 && lastBendAt(12) == 0.0,
         "a curve ending on another affected note must retain its pitch until the following independent attack");
}

void performanceMidiLoweringOrdersDelayedPitchWithSourceWrites() {
  struct Case {
    double noteKey;
    double sourceBend;
    std::vector<std::pair<u64, double>> expected;
  };
  const std::array cases{
      Case{64, 1, {{0, -4}, {1, 1}, {4, -4}, {5, -2}, {6, 0}}},
      Case{64, -4, {{1, -4}, {4, -4}, {5, -2}, {6, 0}}},
      Case{60, 1, {{1, 1}, {4, 0}, {5, 2}, {6, 4}}},
  };
  for (const auto& test : cases) {
    PerformanceTrack track{.id = TrackId{0}, .endTick = 8};
    u64 order = 0;
    u32 noteId = 0, automationId = 0;
    PerformanceEmitter out{track, {track.id, CommandId{1}}, SourceAnnotationId{1}, 0, order, noteId, automationId};
    const auto note = out.note(test.noteKey, 1.0, 8);
    out.at(1).pitchBend(test.sourceBend);
    out.at(4).pitchSlide(note, 60, 64, 2);
    const auto prepared = preparePerformance(PerformanceSequence{
        .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend, .tracks = {track}});
    std::vector<Diagnostic> diagnostics;
    const auto events =
        detail::lowerMidiTrackEvents(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
    std::vector<std::pair<u64, double>> bends;
    for (const auto& event : events) {
      if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event)) {
        bends.emplace_back(bend->header.tick, bend->semitones);
      }
    }
    expect(bends == test.expected,
           "a delayed slide must establish an earlier start only when needed, retaining intervening source writes");
  }
}

void performanceMidiPortamentoReadsTheSourceWheelAtTheTransition() {
  for (bool instrumentRange : {false, true}) {
    PerformanceTrack track{.id = TrackId{0}, .endTick = 8};
    u64 order = 0;
    u32 noteId = 0, automationId = 0;
    PerformanceEmitter out{track, {track.id, CommandId{1}}, SourceAnnotationId{1}, 0, order, noteId, automationId};
    const auto note = out.note(64, 1.0, 8);
    out.at(1).pitchBend(PitchBendPerformanceEvent{.normalizedWheelPosition = 0.5});
    if (instrumentRange) {
      out.at(2).instrument(0, 1);
    } else {
      out.at(2).pitchBendRange(4);
    }
    out.at(4).pitchSlide(note, 66, 68, 2).preferPortamento();
    const SoundBankAsset bank{.instruments = {
        Instrument{.explicitAddress = InstrumentAddress{0, 0}},
        Instrument{.explicitAddress = InstrumentAddress{0, 1}, .pitchBendRangeCents = 400},
    }};
    const auto prepared = preparePerformance({.tracks = {track}}, {bank});
    std::vector<Diagnostic> diagnostics;
    const auto events =
        detail::lowerMidiTrackEvents(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
    bool keepsOriginalKey = false, resetsWheel = false;
    for (const auto& event : events) {
      if (const auto* attack = std::get_if<detail::MidiNoteEvent>(&event); attack && attack->header.tick == 0) {
        keepsOriginalKey = attack->key == 64;
      }
      if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event); bend && bend->header.tick == 4) {
        resetsWheel = bend->semitones == 0 && !bend->normalizedWheelPosition;
      }
    }
    expect(keepsOriginalKey && resetsWheel,
           "portamento must interpret a persistent source wheel using the range at the transition, then reset it");
  }
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
      renderTestMidi(performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);
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
      renderTestMidi(performance, MidiExportOptions{.bankSelectStyle = MidiBankSelectStyle::MsbAndLsb},
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

  const MidiSequence midiSequence = renderTestMidi(performance);
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

  assertPitchBends(renderTestMidi(performance), "synth-modulator MIDI lowering");
  assertPitchBends(
      renderTestMidi(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation),
      "sequence-event MIDI lowering");
}

}  // namespace

void runValueMidiPitchTests() {
  midiNotePlanningResolvesAttacksBeforeRendering();
  midiNotePlanningKeepsZeroDurationAttacksAndOrphanTies();
  midiNotePlanningDoesNotWrapLongExtensions();
  midiNotePlanningKeepsReleasePitchPastExpiredContinuations();
  sourceVoiceKeyChangesNeedNoPitchBinding();
  sourceKeyChangeUsesRealizedPitchWithPortamento();
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
  performanceMidiRendererPlansRangesAtPhysicalAttacks();
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
  performanceMidiLoweringOrdersDelayedPitchWithSourceWrites();
  performanceMidiPortamentoReadsTheSourceWheelAtTheTransition();
  performanceMidiRendererResolvesSourceInstrumentIdentityAtExport();
  performanceMidiRendererQuantizesPitchBendAndPortamento();
  performanceMidiRendererSkipsRedundantPitchBends();
}

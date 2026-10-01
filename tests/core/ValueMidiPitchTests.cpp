/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"

#include "../PerformanceTestSupport.h"
#include "value/export/midi/MidiTrackPlanner.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

using namespace vgmtrans::core;

namespace {

void midiPitchFollowsSourceSensitivityCommands() {
  for (bool normalized : {false, true}) {
    PerformanceTrackFixture fixture{12};
    auto& track = fixture.track;
    auto& out = fixture.out;
    out.pitchBendRange(12);
    out.pitchBend(PitchBendPerformanceEvent{
        .semitones = 1.0, .normalizedWheelPosition = normalized ? std::optional{0.5} : std::nullopt});
    const auto first = out.note(60, 1.0, 4);
    out.at(2).pitchBendRange(1);
    out.at(4).continueVoice(first, NotePerformanceEvent{.key = 60, .durationTicks = 4});
    out.at(5).pitchBendRange(4);
    out.at(8).note(64, 1.0, 4);
    out.at(9).pitchBendRange(4);
    out.at(10).pitchBendRange(2);
    const auto midi = renderTestMidi({.tracks = {track}});
    const auto& events = midi.tracks[0].events;
    const std::vector<std::pair<u64, u16>> expected{{0, 1200}, {2, 100}, {5, 400}, {10, 200}};
    expect(midiPitchBendRanges(events) == expected,
           "source sensitivity changes must keep their ticks; notes, ties, and unused range must not change sensitivity");
    for (const auto& [tick, range] : expected) {
      expect(std::abs(midiPitchSemitonesAt(events, tick) - (normalized ? range / 200.0 : 1.0)) < 0.001,
             "a source range change must reinterpret a normalized wheel and preserve an absolute semitone bend");
    }
    if (normalized) {
      expect(std::ranges::count_if(events, [](const MidiEvent& event) {
        return isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend);
      }) == 1, "following source sensitivity must preserve a persistent normalized wheel without rewriting it");
    }
  }
}

void midiPitchDoesNotAnticipateSourceSensitivity() {
  for (bool hasAttack : {false, true}) {
    PerformanceTrackFixture fixture{12};
    auto& track = fixture.track;
    auto& out = fixture.out;
    out.pitchBend(PitchBendPerformanceEvent{.normalizedWheelPosition = 0.5});
    out.at(4).pitchBendRange(12);
    if (hasAttack) out.at(8).note(60, 1.0, 4);
    const auto midi = renderTestMidi({.tracks = {track}});
    expect(midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 200}, {4, 1200}},
           "lookahead must not move source sensitivity earlier, including before the first attack or without notes");
  }
}

void midiPitchFollowsInstrumentSensitivityAndSourceFallback() {
  PerformanceTrackFixture fixture{10};
  auto& track = fixture.track;
  auto& out = fixture.out;
  out.pitchBendRange(12);
  out.instrument(0, 0);
  out.pitchBend(PitchBendPerformanceEvent{.normalizedWheelPosition = 0.5});
  out.note(60, 1.0, 4);
  out.at(2).instrument(0, 1);
  out.at(3).pitchBendRange(6);
  out.at(4).note(64, 1.0, 4);
  out.at(6).instrument(0, 2);
  const SoundBankAsset bank{.instruments = {
      Instrument{.explicitAddress = InstrumentAddress{0, 0}, .pitchBendRangeCents = 400},
      Instrument{.explicitAddress = InstrumentAddress{0, 1}, .pitchBendRangeCents = 100},
      Instrument{.explicitAddress = InstrumentAddress{0, 2}}}};
  const std::array<const SoundBankAsset*, 1> banks{&bank};
  const auto midi = renderTestMidi({.tracks = {track}}, {}, ModulationConversionPolicy::SynthModulators, banks);
  const auto& events = midi.tracks[0].events;
  expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, 400}, {2, 100}, {6, 600}},
         "instrument sensitivity must take precedence, with the current source setting as fallback");
  expect(std::abs(midiPitchSemitonesAt(events, 3) - 0.5) < 0.001 &&
             std::abs(midiPitchSemitonesAt(events, 6) - 3.0) < 0.001,
         "source changes beneath instrument sensitivity must take effect when the instrument stops overriding it");
}

void midiPitchPreservesSmallAndZeroSourceRanges() {
  for (u16 cents : {0, 1, 30, 100, 235, 12700}) {
    PerformanceTrackFixture fixture{4};
    auto& track = fixture.track;
    auto& out = fixture.out;
    out.pitchBendRange(PitchBendRangePerformanceEvent{.cents = cents});
    out.pitchBend(PitchBendPerformanceEvent{.normalizedWheelPosition = -1.0});
    if (cents == 30) {
      // Independent layers add to the exact source limit despite floating-point roundoff.
      out.pitchBend(-0.1);
      out.pitchBend(-0.2, PitchBendLayerId{1});
    }
    out.note(60, 1.0, 4);
    const auto midi = renderTestMidi({.tracks = {track}});
    expect(midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, cents}},
           "source sensitivities, including zero and fractions of a semitone, must not be rounded or raised");
    expect(std::abs(midiPitchSemitonesAt(midi.tracks[0].events, 0) + cents / 100.0) < 1e-9,
           "the negative wheel endpoint must retain its source pitch scale");
  }
}

void midiPitchKeepsAnOverrideUntilTheSourceRangeSuffices() {
  PerformanceTrackFixture fixture{12};
  auto& track = fixture.track;
  auto& out = fixture.out;
  out.pitchBendRange(2);
  const auto first = out.note(60, 1.0, 4);
  out.at(1).pitchSlide(first, 60, 72, 2);
  out.at(4).note(67, 1.0, 4);
  out.at(4).pitchBend(5.0, PitchBendLayerId{1});
  out.at(5).pitchBend(PitchBendPerformanceEvent{.normalizedWheelPosition = 0.5});
  out.at(6).pitchBendRange(1);
  out.at(7).pitchBendRange(4);
  out.at(8).pitchBend(0.0, PitchBendLayerId{1});
  out.at(8).note(69, 1.0, 4);
  const auto midi = renderTestMidi({.tracks = {track}}, {.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const auto& events = midi.tracks[0].events;
  expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, 1200}, {8, 400}},
         "a necessary override must not shrink while still needed, and must restore the current source range");
  for (const auto& [tick, pitch] : std::vector<std::pair<u64, double>>{
           {3, 12.0}, {4, 5.0}, {5, 6.0}, {6, 5.5}, {7, 7.0}, {8, 2.0}}) {
    expect(std::abs(midiPitchSemitonesAt(events, tick) - pitch) < 0.002,
           "an override must cover the held slide destination and preserve source interpretation beneath it");
  }
}

void midiModulationIgnoresConsumedCommands() {
  PerformanceTrackFixture fixture{4};
  auto& track = fixture.track;
  auto& out = fixture.out;
  out.vibratoRateCyclesPerTick(0.25, {.shape = LfoShape{.samples = {0, 1, 0, -1}}});
  out.vibratoDepth(1.0);
  out.tremoloRateCyclesPerTick(0.25);
  out.tremoloDepth(6.0);
  out.modulation(ModulationPerformanceEvent{
      .target = ModulationPerformanceTarget::PanDepth, .panDepth = 0.5,
      .context = {.cyclesPerTick = 0.25}});
  out.note(60, 1.0, 2);
  out.at(2).updateEnvelope(Envelope{.attackSeconds = 0.1}, EnvelopeFields::Attack);
  out.at(2).note(64, 1.0, 2);
  // An unrelated command must not change sampling, even when all commands
  // at the reset tick have the same sequence number.
  for (auto& event : track.events) {
    std::visit([](auto& value) { value.header.sequence = 0; }, event);
  }
  std::array<std::vector<std::pair<u64, s32>>, 3> pitches;
  std::array<std::vector<std::tuple<u64, u8, s32>>, 3> controllers;
  for (size_t variant = 0; variant < pitches.size(); ++variant) {
    auto input = track;
    if (variant == 1) {
      for (auto& event : input.events) {
        if (const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event)) {
          event = MarkerPerformanceEvent{.header = envelope->header, .text = "Timing reference"};
        }
      }
    }
    if (variant == 2) {
      std::erase_if(input.events, [](const auto& event) { return std::holds_alternative<EnvelopePerformanceEvent>(event); });
    }
    const auto prepared = preparePerformance({.tracks = {input}}, {}, {.dynamicEnvelopes = false});
    const auto midi = renderMidiSequence(prepared, {}, ModulationConversionPolicy::SequenceEventSimulation);
    pitches[variant] = midiPitchBends(midi.tracks[0].events);
    for (const auto& event : midi.tracks[0].events) {
      if (const auto* control = midiChannelMessage(event, MidiChannelMessageKind::ControlChange);
          control && (control->parameter == 10 || control->parameter == 11)) {
        controllers[variant].emplace_back(event.tick, control->parameter, control->value);
      }
    }
  }
  expect(controllers[0] == controllers[1] && controllers[0] == controllers[2],
         "consumed commands and markers must not change tremolo or pan sampling");
  expect(pitches[0] == pitches[1] && pitches[0] == pitches[2],
         "consumed commands and markers must not change pitch sampling at a coincident note reset");
}

void midiModulationRetainsTheSourceTimelineExtent() {
  std::array<std::vector<std::pair<u64, s32>>, 3> pitches;
  for (size_t variant = 0; variant < pitches.size(); ++variant) {
    PerformanceTrackFixture fixture{variant == 0 ? 8u : 4u};
    auto& track = fixture.track;
    auto& out = fixture.out;
    out.vibratoRateCyclesPerTick(0.25, {.shape = LfoShape{.samples = {0, 1, 0, -1}}});
    out.vibratoDepth(1.0);
    out.note(60, 1.0, 4);
    if (variant == 1) out.at(8).timeSignature(3, 4, 24);
    if (variant == 2) out.at(8).updateEnvelope(Envelope{.attackSeconds = 0.1}, EnvelopeFields::Attack);
    const auto prepared = preparePerformance({.tracks = {track}}, {}, {.dynamicEnvelopes = false});
    const auto midi = renderMidiSequence(prepared, {}, ModulationConversionPolicy::SequenceEventSimulation);
    pitches[variant] = midiPitchBends(midi.tracks[0].events);
    expect(std::abs(midiPitchSemitonesAt(midi.tracks[0].events, 8) + 1.0) < 0.001,
           "modulation must continue through the last source command, including the release tail");
  }
  expect(pitches[0] == pitches[1] && pitches[0] == pitches[2],
         "a consumed trailing command must preserve the source timeline extent without needing a timing event");
}

void midiPitchUsesCompletedSamplesForStableSensitivity() {
  PerformanceTrackFixture fixture{10};
  auto& track = fixture.track;
  auto& out = fixture.out;
  out.pitchBend(1.0);
  out.tuning(50.0);
  out.vibratoRateCyclesPerTick(0.25, {.shape = LfoShape{.samples = {0, 1, 0, -1}}});
  out.vibratoDepth(3.0);
  const auto note = out.note(60, 1.0, 4);
  out.at(4).continueVoice(note, NotePerformanceEvent{.key = 60, .durationTicks = 2});
  out.at(5).vibratoDepth(1.0);
  out.at(6).vibratoDepth(0.0);
  out.at(6).note(64, 1.0, 4);
  const auto midi = renderTestMidi({.tracks = {track}}, {}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto& events = midi.tracks[0].events;
  expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, 500}, {6, 200}},
         "sensitivity must cover future combined pitch at the attack, stay fixed through ties, and shrink at the next attack");
  const std::array<double, 7> expected{1.5, 1.5, 4.5, 1.5, -1.5, 1.5, 1.5};
  for (u64 tick = 0; tick < expected.size(); ++tick) {
    expect(std::abs(midiPitchSemitonesAt(events, tick) - expected[tick]) < 0.001,
           "source bend, tuning and vibrato must add in semitones, including when sensitivity changes");
  }
}

void midiPitchMeasuresCombinedAndAsymmetricLfoSamples() {
  for (bool cancel : {false, true}) {
    PerformanceTrackFixture fixture{4};
    auto& track = fixture.track;
    auto& out = fixture.out;
    out.pitchBend(1.5);
    out.vibratoDepth(0.5, {.cyclesPerTick = 0.25,
                          .shape = LfoShape{.samples = {0, 1, 0, -1}},
                          .pitchRangeSemitones = ModulationRange{.minimum = -2.0, .maximum = 6.0}},
                     PitchBendLayerId{1});
    if (cancel) {
      out.vibratoDepth(0.5, {.cyclesPerTick = 0.25,
                            .shape = LfoShape{.samples = {0, -1, 0, 1}},
                            .pitchRangeSemitones = ModulationRange{.minimum = -6.0, .maximum = 2.0}},
                       PitchBendLayerId{2});
    }
    out.note(60, 1.0, 4);
    const auto midi = renderTestMidi({.tracks = {track}}, {}, ModulationConversionPolicy::SynthModulators);
    const auto& events = midi.tracks[0].events;
    expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, static_cast<u16>(cancel ? 200 : 800)}},
           "sensitivity must measure the actual sum of asymmetric LFOs, including cancellation");
    expect(std::abs(midiPitchSemitonesAt(events, 2) - (cancel ? 1.5 : 7.5)) < 0.001 &&
               std::abs(midiPitchSemitonesAt(events, 4) - (cancel ? 1.5 : -0.5)) < 0.001,
           "asymmetric LFO peaks must survive MIDI quantization without clipping");
  }
}

void midiNotePlanningResolvesAttacksBeforeRendering() {
  PerformanceTrackFixture fixture{28};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  const auto lowered = detail::planMidiTrack(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
  std::vector<const detail::MidiNoteBoundary*> notes;
  for (const auto& event : lowered) {
    if (const auto* note = std::get_if<detail::MidiNoteBoundary>(&event)) notes.push_back(note);
  }
  const std::array<bool, 7> hasAttack{true, false, true, false, true, false, false};
  const std::array<u64, 7> ticks{0, 4, 5, 8, 12, 16, 20};
  expect(notes.size() == ticks.size(), "planning must retain every source note boundary");
  for (size_t index = 0; index < notes.size(); ++index) {
    expect(notes[index]->attack.has_value() == hasAttack[index] && notes[index]->header.tick == ticks[index] &&
               notes[index]->expired == (index == 6),
           "completed boundaries must distinguish physical attacks, continuations and expired controller intervals");
  }
  expect(notes[0]->attack->durationTicks == 14 && notes[2]->attack->durationTicks == 20 &&
             notes[4]->attack->durationTicks == 7,
         "completed attacks must include overlap and extensions, clamp the voice deadline, and leave other voices alone");
  expect(!notes[1]->restartVibrato && notes[1]->restartTremolo &&
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
  const auto lowered = detail::planMidiTrack(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
  const auto& zero = std::get<detail::MidiNoteBoundary>(lowered[0]);
  const auto& orphan = std::get<detail::MidiNoteBoundary>(lowered[1]);
  expect(zero.attack && zero.attack->durationTicks == 0 &&
             orphan.attack && orphan.attack->durationTicks == 4 && !orphan.instrument,
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
  PerformanceTrackFixture fixture{14};
  auto& track = fixture.track;
  auto& out = fixture.out;
  const auto first = out.note(NotePerformanceEvent{
      .key = 60, .durationTicks = 4, .maximumDurationMilliseconds = 100.0});
  out.pitchSlide(first, 60, 64, 4).preferPitchBend();
  const auto expired = out.at(4).continueVoice(first, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  out.at(4).pitchSlide(expired, 64, 67, 4).preferPortamento();
  out.at(10).note(72, 1.0, 4);
  const auto prepared = preparePerformance({.timebase = {.ppqn = 10}, .tracks = {track}});
  std::vector<Diagnostic> diagnostics;
  const auto lowered = detail::planMidiTrack(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
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
  expect(midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 400}, {10, 200}},
         "an expired boundary must not start a new pitch-range interval");
}

void performanceMidiRendererChoosesPitchTransitionRepresentationAtLowering() {
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  const auto bendLowering = detail::planMidiTrack(
      bendLoweringInput, 0, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend},
      PerformanceTempoMap{bendLoweringInput.performance()}, bendLoweringDiagnostics);
  const auto sourceNote = std::ranges::find_if(bendLoweringInput.performance().tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  const auto loweredNote = std::ranges::find_if(bendLowering, [](const auto& event) {
    return std::holds_alternative<detail::MidiNoteBoundary>(event);
  });
  expect(sourceNote != bendLoweringInput.performance().tracks[0].events.end() && loweredNote != bendLowering.end(),
         "both the prepared source and completed MIDI boundary must remain available");
  const auto& sourceBoundary = std::get<NotePerformanceEvent>(*sourceNote);
  const auto& midiBoundary = std::get<detail::MidiNoteBoundary>(*loweredNote);
  expect(midiBoundary.header.sourceCommand == sourceBoundary.header.sourceCommand &&
             midiBoundary.header.sourceAnnotation == sourceBoundary.header.sourceAnnotation &&
             midiBoundary.attack && midiBoundary.attack->key == sourceBoundary.key &&
             midiBoundary.attack->durationTicks == sourceBoundary.durationTicks &&
             midiBoundary.attack->linearVelocity == sourceBoundary.linearVelocity,
         "a completed attack must preserve its source association and musical values");
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
    PerformanceTrackFixture fixture{16};
    auto& track = fixture.track;
    auto& out = fixture.out;
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
    const auto lowered = detail::planMidiTrack(prepared, 0, {},
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
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;
  const auto first = out.note(60, 1.0, 4);
  out.pitchSlide(first, 60, 64, 8).preferPitchBend();
  out.at(4).continueVoice(first, NotePerformanceEvent{.key = 67, .durationTicks = 4});
  const auto prepared = preparePerformance({.tracks = {track}});
  std::vector<Diagnostic> diagnostics;
  const auto lowered = detail::planMidiTrack(prepared, 0, {},
                                                     PerformanceTempoMap{prepared.performance()}, diagnostics);
  expect(std::ranges::any_of(lowered, [](const auto& event) {
           const auto* glide = std::get_if<PortamentoPerformanceEvent>(&event);
           return glide && glide->header.tick == 4 && glide->previousKey == 62.0;
         }),
         "an implicit key change must start portamento from the interrupted glide's realized pitch");
}

void performanceMidiRendererAllowsMixedPitchTransitionRendering() {
  PerformanceTrackFixture fixture{12};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  PerformanceTrackFixture fixture{16};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  expect(midiPitchBendAt(midi.tracks[0].events, 8) == 2341 &&
             midiPitchBendAt(midi.tracks[0].events, 12) == 4681 &&
             midiPitchBendAt(midi.tracks[0].events, 16) == 8191,
         "chained transitions should honor each absolute start key without retuning the held voice's bend range");
}

void performanceMidiRendererHonorsRequiredPortamento() {
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  PerformanceTrackFixture fixture{12};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  PerformanceTrackFixture fixture{12};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  expect(midiPitchBendAt(midi.tracks[0].events, 8) == 0,
         "MIDI portamento replacing a held pitch bend should receive an untransposed destination note");
}

void performanceMidiRendererCombinesPitchSlidesWithSimulatedVibrato() {
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;
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


  expect(midiPitchBendAt(synthModulators.tracks[0].events, 2) == -4096,
         "synth-modulator export should retain the slide's unmodulated pitch bend");
  expect(midiPitchBendAt(simulated.tracks[0].events, 2) == -3072,
         "sequence-event export should add simulated vibrato around the active pitch slide");
}

void performanceMidiRendererAddsIndependentPitchLfosWithoutRestartingChannelPhase() {
  PerformanceTrackFixture fixture{6};
  auto& out = fixture.out;
  out.vibratoDepth(1.0, {.cyclesPerTick = 0.25, .shape = LfoShape{.waveform = LfoWaveform::Triangle},
                         .initialPhaseCycles = 0.5, .noteRestartInitialPhaseCycles = 0.5,
                         .restartMode = LfoRestartMode::None, .restartsOnNote = false}, PitchBendLayerId{1});
  out.vibratoDepth(0.5, {.cyclesPerTick = 0.25, .shape = LfoShape{.waveform = LfoWaveform::Triangle},
                         .initialPhaseCycles = 0.0, .noteRestartInitialPhaseCycles = 0.0,
                         .restartMode = LfoRestartMode::None, .restartsOnNote = true}, PitchBendLayerId{2});
  out.note(60, 1.0, 2);
  out.at(2).note(62, 1.0, 4);
  const auto midi = renderTestMidi({.timebase = {.ppqn = 100}, .tracks = {fixture.track}});
  const auto& events = midi.tracks[0].events;
  expect(midiPitchBendAt(events, 2) == -4096,
         "a fresh note should restart only the voice LFO while preserving the additive channel LFO phase");
  expect(midiPitchBendAt(events, 4) == 6144,
         "independent pitch LFO layers should add before conversion to the shared MIDI pitch wheel");
}

void performanceMidiRendererSimulatesDeterministicSampleAndHoldNoise() {
  PerformanceTrackFixture fixture{4};
  fixture.out.vibratoDepth(3.0, {.cyclesPerTick = 0.5, .shape = LfoShape{.waveform = LfoWaveform::Noise},
                                .initialPhaseCycles = 0.0, .restartMode = LfoRestartMode::None,
                                .restartsOnNote = true, .phaseRunsAtZeroDepth = true}, PitchBendLayerId{2});
  fixture.out.note(60, 1.0, 4);
  const auto midi = renderTestMidi({.timebase = {.ppqn = 100}, .tracks = {fixture.track}});
  const auto bends = midiPitchBends(midi.tracks[0].events);
  const auto nonzero = std::ranges::find_if(bends, [](const auto& bend) { return bend.second != 0; });
  expect(nonzero != bends.end() && nonzero->first == 3 &&
             midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 200}},
         "noise should hold zero through its first cycle and size sensitivity from actual samples");
}

void performanceMidiRendererSizesRangeFromActualVibratoSamples() {
  PerformanceTrackFixture fixture{3};
  auto& out = fixture.out;
  out.pitchBendRange(2);
  out.vibratoRate(0.0);
  out.vibratoDepth(0.75);
  out.at(1).pitchBend(2.0);
  out.at(2).vibratoRate(1.0);
  const auto midi = renderTestMidi({.timebase = {.ppqn = 100}, .tracks = {fixture.track}}, {},
                                  ModulationConversionPolicy::SequenceEventSimulation);
  expect(midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 200}},
         "frozen vibrato and an oscillator whose first sample is zero should not reserve unrendered depth");
}

void performanceMidiRendererPreservesFractionalPitchBendRanges() {
  PerformanceTrackFixture fixture{1};
  fixture.out.pitchBendRange(PitchBendRangePerformanceEvent{.cents = 235});
  fixture.out.pitchBend(1.6);
  const auto midi = renderTestMidi({.tracks = {fixture.track}});
  expect(midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 235}} &&
             midiPitchBendAt(midi.tracks[0].events, 0) == 5578,
         "fractional sensitivity must retain its cents byte and scale an absolute semitone bend correctly");
}

void performanceMidiRendererPlansRangesAtPhysicalAttacks() {
  for (bool sharedOrder : {false, true}) {
    PerformanceTrackFixture fixture{10};
    auto& track = fixture.track;
    auto& out = fixture.out;
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
    const std::vector<std::pair<u64, u16>> expectedRanges{{0, 600}, {6, 500}};
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
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;
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

  expect(midiPitchBendAt(plain.tracks[0].events, 7) != midiPitchBendAt(simulated.tracks[0].events, 7),
         "a suppressed destination attack should not restart the held voice's vibrato delay");
}

void performanceMidiRendererPreservesExactSamplesAndChainedPitchContinuity() {
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  const auto bends = midiPitchBends(midi.tracks[0].events);

  expect(ranges == std::vector<std::pair<u64, u16>>{{0, 400}},
         "chained pitch bends should choose one range large enough for the complete note");
  expect(std::ranges::find(bends, std::pair<u64, s32>{1, -5120}) != bends.end() &&
             std::ranges::find(bends, std::pair<u64, s32>{3, -1024}) != bends.end(),
         "pitch-bend lowering should reproduce exact source samples rather than replacing them with a linear ramp");
  expect(std::ranges::none_of(bends, [](const auto& bend) { return bend.first == 2 && bend.second == 0; }),
         "queued pitch transitions should remain continuous at their shared boundary");
}

void performanceMidiRendererKeepsSampledPitchCurvesSparse() {
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;
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

void pitchResetsOnlyAtTheNextAttack() {
  const struct {
    const char* name;
    double noteKey;
    u32 gate, slideTicks;
    std::optional<u64> nextAttack;
    std::vector<std::pair<u64, double>> expected;
  } cases[] = {
      {"interrupted slide", 64, 8, 6, 3, {{0, -4}, {1, -10.0 / 3}, {2, -8.0 / 3}, {3, 0}}},
      {"slide through release", 60, 2, 4, 12, {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {12, 0}}},
      {"terminal release", 60, 8, 4, {}, {{1, 1}, {2, 2}, {3, 3}, {4, 4}}},
  };
  for (const auto& test : cases) {
    PerformanceTrackFixture fixture{16};
    auto& out = fixture.out;
    const auto note = out.note(test.noteKey, 1.0, test.gate);
    out.pitchSlide(note, 60, 64, test.slideTicks).preferPitchBend();
    if (test.nextAttack) out.at(*test.nextAttack).note(67, 1.0, 3);
    const auto midi = renderTestMidi({.tracks = {fixture.track}});
    const auto& events = midi.tracks[0].events;
    const auto bends = midiPitchBends(events);
    expect(bends.size() == test.expected.size(), std::string(test.name) + ": unexpected bend writes");
    for (size_t i = 0; i < bends.size(); ++i) {
      const auto [tick, pitch] = test.expected[i];
      expect(bends[i].first == tick && std::abs(midiPitchSemitonesAt(events, tick) - pitch) < 0.001,
             std::string(test.name) + ": wrong pitch or reset timing at tick " + std::to_string(tick));
    }
  }
}

void performanceMidiLoweringAppliesPitchResetsBeforeLaterTransitions() {
  PerformanceTrackFixture fixture{30};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  const auto lowered = detail::planMidiTrack(
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
  expect(hasRange(0, 1200) && hasRange(6, 800) && hasRange(8, 600) &&
             std::abs(midiPitchSemitonesAt(midi.tracks[0].events, 4) - 4.25) < 0.002 &&
             std::abs(midiPitchSemitonesAt(midi.tracks[0].events, 6) - 3.75) < 0.002 &&
             std::abs(midiPitchSemitonesAt(midi.tracks[0].events, 8) + 0.25) < 0.002,
         "a slide within the source range must follow source sensitivity changes without an override");

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
  const auto sameVoiceLowered = detail::planMidiTrack(
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
  PerformanceTrackFixture fixture{16};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  PerformanceTrackFixture fixture{8};
  auto& track = fixture.track;
  auto& out = fixture.out;

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
  const auto pitchBend = detail::planMidiTrack(
      pitchBendInput, 0, {},
      tempos, pitchBendDiagnostics);
  const auto heldStart = std::ranges::find_if(pitchBend, [](const auto& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick == 4 && bend->layer != kPrimaryPitchBendLayer;
  });
  const auto portamentoInput = prepareTestPerformance(performance, soundBanks);
  std::vector<Diagnostic> portamentoDiagnostics;
  const auto portamento = detail::planMidiTrack(
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
  PerformanceTrackFixture fixture{16};
  auto& track = fixture.track;
  auto& out = fixture.out;
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
  const auto lowered = detail::planMidiTrack(
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
    PerformanceTrackFixture fixture{8};
    auto& track = fixture.track;
    auto& out = fixture.out;
    const auto note = out.note(test.noteKey, 1.0, 8);
    out.at(1).pitchBend(test.sourceBend);
    out.at(4).pitchSlide(note, 60, 64, 2);
    const auto prepared = preparePerformance(PerformanceSequence{
        .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend, .tracks = {track}});
    std::vector<Diagnostic> diagnostics;
    const auto events =
        detail::planMidiTrack(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
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
    PerformanceTrackFixture fixture{8};
    auto& track = fixture.track;
    auto& out = fixture.out;
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
        detail::planMidiTrack(prepared, 0, {}, PerformanceTempoMap{prepared.performance()}, diagnostics);
    bool keepsOriginalKey = false, resetsWheel = false;
    for (const auto& event : events) {
      if (const auto* attack = std::get_if<detail::MidiNoteBoundary>(&event); attack && attack->header.tick == 0) {
        keepsOriginalKey = attack->attack && attack->attack->key == 64;
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
  PerformanceTrackFixture fixture{24};
  fixture.out.pitchBendRange(4);
  fixture.out.pitchBend(1.0);
  fixture.out.at(12).pitchTransitionSettings(83.0);
  const auto midi = renderTestMidi({.tracks = {fixture.track}});
  const auto& events = midi.tracks[0].events;
  expect(midiPitchBendRanges(events) == std::vector<std::pair<u64, u16>>{{0, 400}} &&
             midiPitchBendAt(events, 0) == 2048,
         "MIDI renderer should quantize semitone pitch bend through the active range");
  expect(firstMidiController14(events, MidiController::PortamentoTime) == 83 &&
             std::ranges::any_of(events, [](const MidiEvent& event) {
               return event.tick == 12 && isMidiController(event, MidiController::PortamentoTime);
             }),
         "MIDI renderer should retain both bytes of the physical portamento time at the source tick");
}

void performanceMidiRendererSkipsRedundantPitchBends() {
  PerformanceTrackFixture fixture{48};
  auto& out = fixture.out;
  out.pitchBendRange(2);
  out.pitchBend(0.0);
  out.at(6).pitchBendRange(2);
  out.at(12).pitchBend(0.0);
  out.at(24).pitchBend(1.0);
  out.at(36).pitchBend(1.0);
  out.at(48).pitchBend(0.0);
  for (const auto policy :
       {ModulationConversionPolicy::SynthModulators, ModulationConversionPolicy::SequenceEventSimulation}) {
    const auto midi = renderTestMidi({.tracks = {fixture.track}}, {}, policy);
    expect(midiPitchBendRanges(midi.tracks[0].events) == std::vector<std::pair<u64, u16>>{{0, 200}} &&
               midiPitchBends(midi.tracks[0].events) == std::vector<std::pair<u64, s32>>{{24, 4096}, {48, 0}},
           "both modulation policies should skip repeated pitch bend ranges and values");
  }
}

}  // namespace

void runValueMidiPitchTests() {
  midiPitchFollowsSourceSensitivityCommands();
  midiPitchDoesNotAnticipateSourceSensitivity();
  midiPitchFollowsInstrumentSensitivityAndSourceFallback();
  midiPitchPreservesSmallAndZeroSourceRanges();
  midiPitchKeepsAnOverrideUntilTheSourceRangeSuffices();
  midiModulationIgnoresConsumedCommands();
  midiModulationRetainsTheSourceTimelineExtent();
  midiPitchUsesCompletedSamplesForStableSensitivity();
  midiPitchMeasuresCombinedAndAsymmetricLfoSamples();
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
  performanceMidiRendererSizesRangeFromActualVibratoSamples();
  performanceMidiRendererPreservesFractionalPitchBendRanges();
  performanceMidiRendererPlansRangesAtPhysicalAttacks();
  performanceMidiRendererDoesNotRestartVibratoAtAHeldPitchSlideBoundary();
  performanceMidiRendererPreservesExactSamplesAndChainedPitchContinuity();
  performanceMidiRendererKeepsSampledPitchCurvesSparse();
  pitchResetsOnlyAtTheNextAttack();
  performanceMidiLoweringAppliesPitchResetsBeforeLaterTransitions();
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

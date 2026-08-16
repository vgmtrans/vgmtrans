/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiSnes/SuzukiSnes.h"

#include "value/export/DynamicEnvelope.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::suzuki_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void addCommonDriverTables(std::vector<u8>& bytes) {
  writeBytes(bytes, 0x0300, {0x8f, 0x5d, 0xf2, 0x8f, 0x60, 0xf3});
  writeBytes(bytes, 0x0400,
             {0xd6, 0x48, 0x01, 0x5d, 0xf5, 0x00, 0x50, 0x1c, 0x5d, 0xf5, 0x01, 0x51, 0xd6, 0x60,
              0x01, 0xeb, 0x23, 0xf5, 0x00, 0x52, 0xd6, 0x78, 0x01, 0xf5, 0x01, 0x52, 0xd6, 0x79,
              0x01, 0xf5, 0x00, 0x53, 0xd6, 0xa8, 0x01, 0xf5, 0x01, 0x53, 0xd6, 0xa9, 0x01});

  std::fill(bytes.begin() + 0x5000, bytes.begin() + 0x5080, 0xff);
  bytes[0x5000] = 0;
  bytes[0x5101] = 0x40;
  bytes[0x5200] = 0x8f;
  bytes[0x5201] = 0xe0;
  // Signed little-endian 8.8 value -0.5 semitones.
  bytes[0x5300] = 0x80;
  bytes[0x5301] = 0xff;

  writeLe16(bytes, 0x6000, 0x6100);
  writeLe16(bytes, 0x6002, 0x6100);
  bytes[0x6100] = 0x01;  // one terminal BRR block
}

void addSd3SongLoader(std::vector<u8>& bytes, u16 header) {
  writeBytes(bytes, 0x0100,
             {0xfa, 0xf5, 0x5c, 0xfa, 0x5c, 0xf5, 0x3f, 0x0f, 0x0a, 0xcd, 0x00, 0xe4, 0x1a, 0x1c,
              0xfd, 0xf5, static_cast<u8>(header), static_cast<u8>(header >> 8), 0xd6, 0x79, 0x1b, 0xf5,
              0x01, 0x20, 0xd6, 0x7a, 0x1b, 0x3d, 0x3d});
}

void addLaterSongLoader(std::vector<u8>& bytes, u16 header) {
  writeBytes(bytes, 0x0100,
             {0xfa, 0xf5, 0x5f, 0x3f, 0xfe, 0x09, 0x3f, 0x8a, 0x04, 0x8f, 0x08, 0x06, 0xe4, 0x1d,
              0x1c, 0x5d, 0xf6, static_cast<u8>(header), static_cast<u8>(header >> 8), 0xd5, 0x4c, 0x1b,
              0xf6, 0x01, 0x20, 0xd5, 0x4d, 0x1b, 0x3d, 0x3d});
}

void addLaterDispatch(std::vector<u8>& bytes, u16 lengths) {
  writeBytes(bytes, 0x0700,
             {0x80, 0xa8, 0xc4, 0x2d, 0x5d, 0xf5, static_cast<u8>(lengths), static_cast<u8>(lengths >> 8),
              0x28, 0x07, 0xc4, 0x06, 0x8d, 0x00, 0xcd, 0x00, 0x8b, 0x06, 0xf0, 0x09, 0xf7, 0x29,
              0xd4, 0x0e, 0x3a, 0x29, 0x3d, 0x2f, 0xf3, 0xae, 0x1c, 0x5d, 0x60, 0xeb, 0x1e, 0x1f,
              0xa9, 0x16});
}

std::vector<u8> sd3Fixture() {
  constexpr u16 header = 0x2000;
  constexpr u16 track = 0x3000;
  std::vector<u8> bytes(kAramSize);
  addSd3SongLoader(bytes, header);
  addCommonDriverTables(bytes);
  writeLe16(bytes, header, track);
  writeBytes(bytes, header + 16, {0x02, 0x00, 0x40, 0x40, 0x80, 0x80});
  writeBytes(bytes, track, {0xde, 0x00, 0xee, 0xa8, 0xef, 0xd0});
  return bytes;
}

std::vector<u8> laterFixture(bool smr) {
  constexpr u16 header = 0x2000;
  constexpr u16 track = 0x3000;
  constexpr u16 lengths = 0x7000;
  std::vector<u8> bytes(kAramSize);
  addLaterSongLoader(bytes, header);
  addCommonDriverTables(bytes);
  addLaterDispatch(bytes, lengths);
  bytes[lengths + 56] = smr ? 4 : 1;
  writeBytes(bytes, header, {0x02, 0x00, 0x40, 0x40, 0x80, 0x80});
  writeLe16(bytes, header + 6, track);
  bytes[track] = 0xd0;
  return bytes;
}

template <class Event>
std::vector<const Event*> events(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

PerformanceSequence render(Version version, std::vector<u8> bytes) {
  const auto& config = sequenceConfig();
  SequenceProgram program{
      .runtime = sequenceRuntime(version),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {decodeSourceTrack(ByteReader(SourceId{121}, bytes), version, 0, 0)},
  };
  program.behavior.initialTempoMicrosecondsPerQuarter = version == Version::SeikenDensetsu3 ? 576000 : 372000;
  program.behavior.initialSourceInstrument = InstrumentIdentity{
      .domain = std::string(kInstrumentDomain),
      .key = version == Version::SeikenDensetsu3 ? 5u : (version == Version::BahamutLagoon ? 6u : 4u),
  };
  program.behavior.initialLevel = version == Version::SeikenDensetsu3
                                      ? 0x3c / 128.0
                                      : (version == Version::BahamutLagoon ? 0x50 / 128.0 : 0x64 / 128.0);
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

void layoutsAndHeadersAreVersioned() {
  const auto sd3 = findLayout(ByteReader(SourceId{122}, sd3Fixture()));
  expect(sd3 && sd3->version == Version::SeikenDensetsu3 && sd3->sequenceHeaderAddress == 0x2000 &&
             sd3->srcnTableAddress == 0x5000 && sd3->volumeTableAddress == 0x5101,
         "SD3 signature should recover the song header and all four split instrument tables");

  const auto bl = findLayout(ByteReader(SourceId{123}, laterFixture(false)));
  const auto smr = findLayout(ByteReader(SourceId{124}, laterFixture(true)));
  expect(bl && bl->version == Version::BahamutLagoon && smr && smr->version == Version::SuperMarioRpg,
         "the FC operand length should distinguish SMR from the otherwise shared Bahamut Lagoon driver");

  const SequenceParse sd3Sequence = decodeSequence(ByteReader(SourceId{125}, sd3Fixture()), *sd3, AssetId{125});
  const SequenceParse blSequence = decodeSequence(ByteReader(SourceId{126}, laterFixture(false)), *bl, AssetId{126});
  expect(sd3Sequence.program.tracks.size() == 1 && sd3Sequence.headerRange.size == 22 &&
             sd3Sequence.recipes.drums.size() == 1 && sd3Sequence.recipes.drums.front().sourceProgram == 0,
         "SD3 should decode pointers before its sequence-owned drum recipe");
  expect(blSequence.program.tracks.size() == 1 && blSequence.headerRange.size == 22 &&
             blSequence.recipes.drums.size() == 1 && blSequence.program.tracks.front().startAddress.value == 0x3000,
         "later drivers should decode the same immutable recipe before their track pointers");
}

void playbackUsesAuditedGatingPitchAndLoops() {
  const PerformanceSequence gated = render(Version::SeikenDensetsu3, {
                                                                            0xdd,
                                                                            0x08,
                                                                            0xcf,
                                                                            0x08,
                                                                            0xec,
                                                                            0x01,
                                                                            0xde,
                                                                            0x03,
                                                                            0xee,
                                                                            0xa8,
                                                                            0xef,
                                                                            0xd0,
                                                                        });
  const auto notes = events<NotePerformanceEvent>(gated.tracks.front());
  const auto tunings = events<TuningPerformanceEvent>(gated.tracks.front());
  const auto instruments = events<InstrumentPerformanceEvent>(gated.tracks.front());
  expect(
      gated.diagnostics.empty() && notes.size() == 1 && notes.front()->key == 60.0 &&
             notes.front()->durationTicks == 2,
         "duration rate 8 should gate a three-tick percussion note after two ticks");
  expect(!tunings.empty() && std::abs(tunings.back()->cents - 75.0) < 0.000001,
         "CF sixteenth-semitone tuning and EC quarter-semitone transpose should retain their fractions");
  expect(instruments.size() >= 4 && instruments[instruments.size() - 2]->sourceInstrument->key == kDrumKitKey &&
             instruments.back()->sourceInstrument->key == 3,
         "percussion mode should select the derived kit and restore the last melodic program afterward");

  const PerformanceSequence repeated = render(Version::SeikenDensetsu3, {
                                                                               0xd4,
                                                                               0x02,
                                                                               0xa8,
                                                                               0xd6,
                                                                               0xc4,
                                                                               0xd5,
                                                                               0xd0,
                                                                           });
  const auto repeatedNotes = events<NotePerformanceEvent>(repeated.tracks.front());
  expect(repeated.diagnostics.empty() && repeatedNotes.size() == 2 && repeatedNotes[0]->key == 72.0 &&
             repeatedNotes[1]->key == 72.0,
         "repeat break should branch only on the final pass and repeat end should restore the saved octave");
}

void driverDefaultsAndPitchTransitionsAreVersioned() {
  struct ExpectedDefaults {
    Version version;
    u32 duration;
    u32 program;
    double level;
  };
  constexpr std::array expectedDefaults{
      ExpectedDefaults{Version::SeikenDensetsu3, 3, 5, 0x3c / 128.0},
      ExpectedDefaults{Version::BahamutLagoon, 2, 6, 0x50 / 128.0},
      ExpectedDefaults{Version::SuperMarioRpg, 2, 4, 0x64 / 128.0},
  };
  for (const ExpectedDefaults expected : expectedDefaults) {
    const PerformanceSequence performance = render(expected.version, {0xa8, 0xd0});
    const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
    const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks.front());
    const auto levels = events<LevelPerformanceEvent>(performance.tracks.front());
    expect(notes.size() == 1 && notes.front()->durationTicks == expected.duration,
           "the profile-specific initial duration rate should match the driver reset state");
    expect(!instruments.empty() && instruments.front()->sourceInstrument &&
               instruments.front()->sourceInstrument->key == expected.program,
           "the profile-specific initial instrument should match the driver reset state");
    expect(!levels.empty() && std::abs(levels.front()->linearGain - expected.level) < 0.000001,
           "the profile-specific initial channel volume should match the driver reset state");
  }

  const PerformanceSequence sd3 = render(Version::SeikenDensetsu3, {0x3c, 0x6f, 0xe5, 0x04, 0xfe, 0x29, 0xd0});
  const PerformanceSequence later = render(Version::BahamutLagoon, {0x3c, 0x6f, 0xe5, 0x04, 0xfe, 0x29, 0xd0});
  const auto* sd3Slide =
      sd3.tracks.front().automations.empty() ? nullptr : pitchTransitionIntent(sd3.tracks.front().automations.front());
  const auto* laterSlide = later.tracks.front().automations.empty()
                               ? nullptr
                               : pitchTransitionIntent(later.tracks.front().automations.front());
  expect(sd3Slide != nullptr && sd3Slide->timing.timelineTicks == 3 && laterSlide != nullptr &&
             laterSlide->timing.timelineTicks == 4,
         "SD3 should decrement E5's pitch-slide duration while the later drivers should use its raw duration");

  // And my Name's Booster, track 0 at ARAM $2035 and $203B: each E5 precedes the note it bends.
  const PerformanceSequence booster =
      render(Version::SuperMarioRpg, {0xc6, 0x05, 0xe5, 0x18, 0x02, 0x38, 0x41, 0xd7, 0xe5, 0x24, 0x02, 0x21, 0xd0});
  const auto boosterNotes = events<NotePerformanceEvent>(booster.tracks.front());
  const auto& boosterAutomations = booster.tracks.front().automations;
  const auto* firstBoosterSlide =
      boosterAutomations.empty() ? nullptr : pitchTransitionIntent(boosterAutomations.front());
  const auto* thirdBoosterSlide =
      boosterAutomations.size() < 2 ? nullptr : pitchTransitionIntent(boosterAutomations[1]);
  const MidiSequence boosterMidi =
      renderMidiSequence(booster, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const bool thirdNoteBendsUp = std::ranges::any_of(boosterMidi.tracks.front().events, [](const MidiEvent& event) {
    const auto* bend = std::get_if<PitchBend>(&event);
    return bend != nullptr && bend->tick > 96 && bend->value > 0;
  });
  expect(boosterNotes.size() == 3 && firstBoosterSlide != nullptr && thirdBoosterSlide != nullptr &&
             firstBoosterSlide->note == boosterNotes.front()->note && firstBoosterSlide->startKey == 60.0 &&
             firstBoosterSlide->targetKey == 62.0 && firstBoosterSlide->timing.timelineTicks == 0x18 &&
             thirdBoosterSlide->note == boosterNotes[2]->note && thirdBoosterSlide->startKey == 65.0 &&
             thirdBoosterSlide->targetKey == 67.0 && thirdBoosterSlide->timing.timelineTicks == 0x24 &&
             thirdNoteBendsUp,
         "each SMR E5 should slide the note immediately following it upward by two semitones");

  const PerformanceSequence automatic = render(Version::BahamutLagoon, {0xa8, 0xf6, 0x04, 0xaa, 0xd0});
  const auto automaticNotes = events<NotePerformanceEvent>(automatic.tracks.front());
  const auto* automaticSlide = automatic.tracks.front().automations.empty()
                                   ? nullptr
                                   : pitchTransitionIntent(automatic.tracks.front().automations.front());
  expect(automaticNotes.size() == 2 && automaticSlide != nullptr &&
             automaticSlide->note == automaticNotes.back()->note && !automaticSlide->previousNote &&
             automaticSlide->startKey == 72.0 && automaticSlide->targetKey == 74.0 &&
             automaticSlide->timing.timelineTicks == 4,
         "later-driver F6 should glide each newly attacked note from the preceding note's pitch");

  const PerformanceSequence repeatedSlide =
      render(Version::SuperMarioRpg, {0xa8, 0xe6, 0xe5, 0x02, 0x02, 0xc3, 0x04, 0xe6, 0xd0});
  const auto& repeatedAutomations = repeatedSlide.tracks.front().automations;
  const auto* repeatedTail =
      repeatedAutomations.size() == 2 ? pitchTransitionIntent(repeatedAutomations.back()) : nullptr;
  expect(repeatedTail != nullptr && repeatedAutomations.front().realization.startTick == 3 &&
             repeatedAutomations.front().realization.endTick == 5 && repeatedTail->startKey == 74.0 &&
             repeatedTail->timing.timelineTicks == 256 && repeatedAutomations.back().realization.endTick == 7 &&
             repeatedAutomations.back().realization.endReason == PerformanceAutomationEndReason::Interrupted,
         "E6 should keep E5 moving past its duration until the next E6 stops the repeated slide");
}

void smrBowserPitchSlideContinuesAcrossTies() {
  // Fight Against Bowser, track 0 at ARAM $2081: E5 C0 FE, tie 29,
  // volume fade E4 90 00, then tie 1B.
  const PerformanceSequence performance =
      render(Version::SuperMarioRpg, {0x3c, 0x6f, 0xe5, 0xc0, 0xfe, 0x29, 0xe4, 0x90, 0x00, 0x1b, 0xd0});
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto& automations = performance.tracks.front().automations;
  const auto slide = std::ranges::find_if(automations, [](const PerformanceAutomation& automation) {
    return pitchTransitionIntent(automation) != nullptr;
  });
  const auto* intent = slide == automations.end() ? nullptr : pitchTransitionIntent(*slide);

  expect(notes.size() == 4 &&
             std::ranges::all_of(notes,
                                 [&](const NotePerformanceEvent* note) { return note->note == notes.front()->note; }),
         "the source note and its three ties should remain one sounding voice");
  expect(intent != nullptr && intent->note == notes.front()->note && intent->startKey == 76.0 &&
             intent->targetKey == 74.0 && intent->timing.timelineTicks == 0xc0 && slide->realization.startTick == 72 &&
             slide->realization.endTick == 264,
         "SMR E5 C0 FE should slide E5 down to D5 for 192 ticks across the following ties");

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(std::ranges::any_of(midi.tracks.front().events,
                             [](const MidiEvent& event) {
                               const auto* bend = std::get_if<PitchBend>(&event);
                               return bend != nullptr && bend->value != 0;
                             }),
         "SMR pitch slide should lower to audible MIDI pitch-bend events");
}

void laterE0UsesTheSustainRateAsAGatedRelease() {
  // The Road is Full of Dangers, track 0 at ARAM $2033: E0 1B. The later
  // driver clears SR at note attack and restores 1B only when the gate ends.
  const PerformanceSequence performance = render(Version::SuperMarioRpg, {0xde, 0x1e, 0xe0, 0x1b, 0x85, 0xd0});
  const auto envelopes = events<EnvelopePerformanceEvent>(performance.tracks.front());
  expect(envelopes.size() == 1 && envelopes.front()->update.values &&
             envelopes.front()->update.fields == (EnvelopeFields::SecondDecay | EnvelopeFields::Release) &&
             envelopes.front()->update.values->secondDecaySeconds &&
             std::isinf(*envelopes.front()->update.values->secondDecaySeconds) &&
             envelopes.front()->update.values->releaseSeconds &&
             std::abs(*envelopes.front()->update.values->releaseSeconds - snesDspAdsrSustainSeconds(0x1b)) < 0.000001,
         "later-driver E0 should hold the sustain during the note and apply its rate only at gate release");

  std::vector<InstrumentSetAsset> sets{InstrumentSetAsset{
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 30},
          .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 30},
          .regions = {Region{.envelope =
                                 Envelope{
                                     .attackSeconds = 0.0,
                                     .decaySeconds = 0.0,
                                     .secondDecaySeconds = 0.01,
                                     .releaseSeconds = 0.128,
                                     .sustainAmplitude = 1.0,
                                 }}},
      }},
  }};
  const auto materialized = materializeDynamicEnvelopes(performance, sets);
  const auto notes = events<NotePerformanceEvent>(materialized.performance.tracks.front());
  expect(notes.size() == 1 && notes.front()->instrumentAddress,
         "E0 should select a materialized envelope variant for the following attack");
  const auto variant = std::ranges::find_if(sets.front().instruments, [&](const Instrument& instrument) {
    return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) ==
           *notes.front()->instrumentAddress;
  });
  expect(variant != sets.front().instruments.end() && variant->regions.size() == 1 &&
             variant->regions.front().envelope.secondDecaySeconds &&
             std::isinf(*variant->regions.front().envelope.secondDecaySeconds) &&
             variant->regions.front().envelope.releaseSeconds &&
             std::abs(*variant->regions.front().envelope.releaseSeconds - snesDspAdsrSustainSeconds(0x1b)) < 0.000001,
         "the generated E0 variant must not decay toward silence from the start of the note");

  const PerformanceSequence normalSustain = render(Version::SuperMarioRpg, {0xe0, 0x1b, 0xdc, 0x08, 0x85, 0xd0});
  const auto restored = events<EnvelopePerformanceEvent>(normalSustain.tracks.front());
  expect(restored.size() == 3 && restored[1]->update.fields == EnvelopeFields::SecondDecay &&
             restored[2]->update.fields == EnvelopeFields::Release && !restored[2]->update.values,
         "DC should disable E0's gated release while installing its ordinary sustain rate");
}

void modulationMathMatchesEachDriverRevision() {
  const auto modulationValue = [](const PerformanceSequence& performance, ModulationPerformanceTarget target) {
    const auto modulation = std::ranges::find_if(performance.tracks.front().events, [&](const PerformanceEvent& event) {
      const auto* candidate = std::get_if<ModulationPerformanceEvent>(&event);
      return candidate != nullptr && candidate->target == target;
    });
    return modulation == performance.tracks.front().events.end()
               ? static_cast<const ModulationPerformanceEvent*>(nullptr)
               : std::get_if<ModulationPerformanceEvent>(&*modulation);
  };

  const PerformanceSequence sd3 = render(Version::SeikenDensetsu3, {0xf0, 0x08, 0x04, 0xe9, 0x08, 0x04, 0xd0});
  const PerformanceSequence smr = render(Version::SuperMarioRpg, {0xf0, 0x08, 0x08, 0xe9, 0x08, 0x04, 0xd0});
  const auto* sd3Vibrato = modulationValue(sd3, ModulationPerformanceTarget::VibratoDepth);
  const auto* sd3Pan = modulationValue(sd3, ModulationPerformanceTarget::PanDepth);
  const auto* smrVibrato = modulationValue(smr, ModulationPerformanceTarget::VibratoDepth);
  const auto* smrPan = modulationValue(smr, ModulationPerformanceTarget::PanDepth);
  expect(sd3Vibrato && sd3Vibrato->pitchDepthSemitones == 0.5 && sd3Pan && sd3Pan->panDepth == 0.25,
         "SD3 modulation should use its step-times-period accumulator scaling");
  expect(smrVibrato && smrVibrato->pitchDepthSemitones == 0.28125 && smrPan && smrPan->panDepth == 0.03125,
         "SMR modulation should use the later driver's divided vibrato and fixed-excursion pan scaling");

  const PerformanceSequence blOneSided = render(Version::BahamutLagoon, {0xe9, 0x88, 0x04, 0xd0});
  const auto* blPan = modulationValue(blOneSided, ModulationPerformanceTarget::PanDepth);
  expect(blPan && blPan->panDepth == 0.25 && blPan->cyclesPerTick == 0.0625 &&
             blPan->polarity == LfoPolarity::Positive && blPan->initialPhaseCycles == 0.75,
         "BL's high pan-LFO period bit should select a one-sided two-period triangle");

  // And My Name's Booster, track 0 at ARAM $202A: F4 07 36. Its $36 step
  // wraps the 7-bit volume accumulator several times before each reversal.
  const PerformanceSequence booster =
      render(Version::SuperMarioRpg, {0xf4, 0x07, 0x36, 0xde, 0x5e, 0xb6, 0x20, 0xd0});
  const auto* boosterDepth = modulationValue(booster, ModulationPerformanceTarget::TremoloDepth);
  const auto* boosterRate = modulationValue(booster, ModulationPerformanceTarget::TremoloRate);
  expect(boosterDepth && boosterDepth->volumeDepthLinearGain == 1.0 && boosterDepth->shape &&
             boosterDepth->shape->waveform == LfoWaveform::SawtoothDown &&
             boosterDepth->polarity == LfoPolarity::Negative && boosterDepth->initialPhaseCycles == 0.0 &&
             boosterDepth->sampleImmediatelyOnNote && boosterDepth->directionReversalTicks == 7 &&
             boosterDepth->tremoloGainMode == TremoloGainMode::NoBoost && boosterRate &&
             std::abs(boosterRate->cyclesPerTick.value_or(0.0) - (0x36 / 128.0)) < 0.000001,
         "SMR F4 07 36 should preserve the fast carrier created by its wrapped 7-bit volume steps");
  const SequenceModulationProfile boosterProfile = analyzeSequenceModulation(booster);
  expect(boosterProfile.instruments.tremolo &&
             boosterProfile.instruments.tremolo->gainMode == TremoloGainMode::NoBoost,
         "linear-gain tremolo planning should retain Suzuki's attenuation-first oscillator");
  const MidiSequence boosterMidi =
      renderMidiSequence(booster, {}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto boosterExpressionAt = [&](u64 tick) -> std::optional<u8> {
    for (const MidiEvent& event : boosterMidi.tracks.front().events) {
      if (const auto* expression = std::get_if<Expression>(&event);
          expression != nullptr && expression->tick == tick) {
        return expression->value;
      }
    }
    return std::nullopt;
  };
  expect(boosterExpressionAt(6) == boosterExpressionAt(8) &&
             boosterExpressionAt(7).value_or(127) < boosterExpressionAt(6).value_or(0) &&
             boosterExpressionAt(14) == 127,
         "SMR folded-tremolo simulation should reverse after seven ticks and return to nominal after fourteen");

  const PerformanceSequence shortTremolo =
      render(Version::SuperMarioRpg, {0xf4, 0x04, 0x10, 0xb6, 0x10, 0xd0});
  const MidiSequence shortTremoloMidi =
      renderMidiSequence(shortTremolo, {}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto expressionAt = [&](u64 tick) -> std::optional<u8> {
    for (const MidiEvent& event : shortTremoloMidi.tracks.front().events) {
      if (const auto* expression = std::get_if<Expression>(&event);
          expression != nullptr && expression->tick == tick) {
        return expression->value;
      }
    }
    return std::nullopt;
  };
  expect(expressionAt(4).value_or(127) < 127 && expressionAt(8) == 127,
         "sequence-event MIDI should reach Suzuki's attenuation extreme after one period and return after two");

  const PerformanceSequence wrappedPeriod = render(Version::SuperMarioRpg, {0xf4, 0x00, 0x01, 0xd0});
  const auto* wrappedRate = modulationValue(wrappedPeriod, ModulationPerformanceTarget::TremoloRate);
  expect(wrappedRate && std::abs(wrappedRate->cyclesPerTick.value_or(0.0) - (1.0 / 128.0)) < 0.000001,
         "a zero counter reload should retain the wrapped accumulator's step-driven carrier");

  const PerformanceSequence sd3Folded = render(Version::SeikenDensetsu3, {0xf4, 0x07, 0x36, 0xd0});
  const auto* sd3FoldedRate = modulationValue(sd3Folded, ModulationPerformanceTarget::TremoloRate);
  expect(sd3FoldedRate && std::abs(sd3FoldedRate->cyclesPerTick.value_or(0.0) - (0x36 / 256.0)) < 0.000001,
         "SD3 folded tremolo should retain its eight-bit accumulator carrier");

  const PerformanceSequence restarted = render(Version::SeikenDensetsu3, {0xe9, 0x08, 0x04, 0xeb, 0xea, 0xd0});
  std::vector<double> panDepths;
  for (const PerformanceEvent& event : restarted.tracks.front().events) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    if (modulation != nullptr && modulation->target == ModulationPerformanceTarget::PanDepth && modulation->panDepth) {
      panDepths.push_back(*modulation->panDepth);
    }
  }
  expect(panDepths == std::vector<double>{0.25, 0.0, 0.25},
         "SD3 EA should restart the saved pan LFO after EB disables it");

  const PerformanceSequence zeroLengthFades =
      render(Version::SuperMarioRpg, {0xe4, 0x00, 0x00, 0xe8, 0x00, 0x00, 0xd0});
  expect(zeroLengthFades.tracks.front().automations.empty(),
         "zero-length E4 and E8 commands should be no-ops, as in the SPC700 drivers");
}

void scannerBuildsSequenceDerivedDrumKit() {
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "SuzukiSnes fixture.aram"}, sd3Fixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "SuzukiSnes fixture should publish one complete source collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.sequence && collection.members.instrumentSets.size() == 1 &&
             collection.members.sampleCollections.size() == 1,
         "SuzukiSnes collection should connect its sequence, instruments, and BRR samples");

  const auto* set = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(set != nullptr && set->instruments.size() == 2,
         "one melodic program and its sequence-derived drum kit should share one immutable instrument set");
  const auto kit = std::ranges::find_if(set->instruments, [](const Instrument& instrument) {
    return instrument.identity && instrument.identity->key == kDrumKitKey;
  });
  expect(kit != set->instruments.end() && kit->regions.size() == 1,
         "the decoded sequence recipe should materialize one drum region during scanning");
  const Region& drum = kit->regions.front();
  expect(drum.keyRange.low == 62 && drum.keyRange.high == 62 && std::abs(drum.unityKey - 67.5) < 0.000001 &&
             std::abs(drum.pan - 0.5) < 0.000001 && std::abs(drum.attenuationDb - 6.020599913) < 0.000001,
         "drum key remapping, signed 8.8 tuning, center pan, and 7-bit gain should match the SPC driver");
}

}  // namespace

void runSuzukiSnesModuleTests() {
  layoutsAndHeadersAreVersioned();
  playbackUsesAuditedGatingPitchAndLoops();
  driverDefaultsAndPitchTransitionsAreVersioned();
  smrBowserPitchSlideContinuesAcrossTies();
  laterE0UsesTheSustainRateAsAGatedRelease();
  modulationMathMatchesEachDriverRevision();
  scannerBuildsSequenceDerivedDrumKit();
}

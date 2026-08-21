/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ItikitiSnes/ItikitiSnes.h"

#include "value/base/LevelScale.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceMotion.h"
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
using namespace vgmtrans::formats::itikiti_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
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

std::vector<const ModulationPerformanceEvent*> modulationEvents(const PerformanceTrack& track,
                                                                ModulationPerformanceTarget target) {
  auto result = events<ModulationPerformanceEvent>(track);
  std::erase_if(result, [=](const ModulationPerformanceEvent* event) { return event->target != target; });
  return result;
}

PerformanceSequence render(std::vector<u8> bytes, u8 group = 0, u8 echoDelay = 4) {
  const SequenceProgramConfig& config = sequenceConfig();
  SequenceProgram program{
      .runtime = sequenceRuntime(echoDelay),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {decodeSourceTrack(ByteReader(SourceId{201}, bytes), 0, 0, 0, group)},
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

std::vector<u8> scannerFixture() {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x0100, {0xed, 0x6b, 0xde, 0xf8, 0xa1, 0xf5, 0x80, 0xed, 0xc4, 0x02, 0xf5,
                             0x81, 0xed, 0xc4, 0x03, 0x8d, 0x01, 0xe4, 0xef, 0x77, 0x02});
  writeBytes(bytes, 0x0200, {0xf5, 0x40, 0x24, 0xd6, 0x80, 0xef, 0xf5, 0x41, 0x24, 0xd6, 0x81, 0xef,
                             0xf5, 0x60, 0x25, 0xd6, 0x00, 0xee, 0xf5, 0x61, 0x25, 0x2f, 0x00});

  // The live group pointer names the byte after the sequence header. Raw
  // track offsets are relative to the recovered sequence base, not the header.
  writeLe16(bytes, 0xed80, 0x3004);
  bytes[0xed90] = 1;
  writeBytes(bytes, 0x3000, {4, 1, 0x10, 0x00, 0x10, 2, 0x37, 8, 0});

  writeBytes(bytes, 0x2440, {0x00, 0x00});
  writeBytes(bytes, 0x2560, {0x6f, 0xa7});
  writeBytes(bytes, 0x2444, {0x40, 0x00});
  writeBytes(bytes, 0x2564, {0x4d, 0xb2});
  writeLe16(bytes, 0x2200, 0x4000);
  writeLe16(bytes, 0x2202, 0x4000);
  writeLe16(bytes, 0x2208, 0x4010);
  writeLe16(bytes, 0x220a, 0x4010);
  writeBytes(bytes, 0x4000, {1, 0, 0, 0, 0, 0, 0, 0, 0});
  writeBytes(bytes, 0x4010, {1, 0, 0, 0, 0, 0, 0, 0, 0});
  return bytes;
}

void layoutAndSynthFollowRelocatedDriverTables() {
  const std::vector<u8> bytes = scannerFixture();
  const auto layout = findLayout(ByteReader(SourceId{202}, bytes));
  expect(layout && layout->sequenceHeaderAddress == 0x3000 && layout->sequenceBaseAddress == 0x2ff4 &&
             layout->tuningTableAddress == 0x2440 && layout->adsrTableAddress == 0x2560 &&
             layout->spcDirAddress == 0x2200 && layout->trackCount == 1 && layout->echoDelay == 4,
         "ItikitiSnes signatures should recover the header, sequence base, tables, DIR, voices, and EDL");

  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "ItikitiSnes fixture.aram"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection* collection = snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
  expect(snapshot.diagnostics().empty() && snapshot.collections().size() == 1 && collection->members.sequence &&
             collection->members.soundBanks.size() == 1 && collection->members.samplePools.empty(),
         "ItikitiSnes scanning should publish one sequence and self-contained sound bank");

  const auto* set = snapshot.asset<SoundBankAsset>(collection->members.soundBanks.front());
  const auto found = set == nullptr ? std::vector<Instrument>::const_iterator{}
                                    : std::ranges::find(set->instruments, u64{2}, [](const Instrument& value) {
                                        return value.identity ? value.identity->key : ~u64{0};
                                      });
  const Instrument* instrument = set != nullptr && found != set->instruments.end() ? &*found : nullptr;
  expect(instrument != nullptr && instrument->regions.size() == 1 &&
             std::abs(instrument->regions.front().unityKey - (72.0 - std::log2(1.5) * 12.0)) < 0.000001,
         "big-endian driver tuning should set the referenced SRCN's fractional unity key");
}

void groupFallbackAndProgramBanksMatchTheDriver() {
  std::vector<u8> bytes = scannerFixture();
  writeLe16(bytes, 0xed80, 0);
  writeLe16(bytes, 0xed82, 0x3104);
  bytes[0xed92] = 1;
  writeBytes(bytes, 0x3100, {5, 1, 0x10, 0x00, 0x00});
  const auto layout = findLayout(ByteReader(SourceId{203}, bytes));
  expect(layout && layout->groupIndex == 1 && layout->sequenceHeaderAddress == 0x3100 && layout->echoDelay == 0,
         "an inactive invalid music pointer should fall back to the active valid driver group");

  const PerformanceSequence performance = render({0x10, 0x22, 0}, 1);
  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && std::ranges::any_of(instruments,
                                                                [](const auto* event) {
                                                                  return event->sourceInstrument &&
                                                                         event->sourceInstrument->key == 0x32;
                                                                }),
         "upper SRCNs should receive the non-music group's audited $10-program bank offset");
}

void physicalLfosAndMixerStateArePreserved() {
  const PerformanceSequence performance =
      render({0x19, 2,    4,    0xa0, 0x1b, 3,    2,    0xa0, 0x1d, 4,    0x40, 0x02, 0x40,
              0x04, 0xc0, 0x00, 0x04, 0x7f, 0x01, 0x23, 0x24, 0x27, 0x83, 0x0e, 0x40, 0});
  const PerformanceTrack& track = performance.tracks.front();
  const auto vibrato = modulationEvents(track, ModulationPerformanceTarget::VibratoDepth);
  const auto vibratoRate = modulationEvents(track, ModulationPerformanceTarget::VibratoRate);
  const auto tremolo = modulationEvents(track, ModulationPerformanceTarget::TremoloDepth);
  const auto tremoloRate = modulationEvents(track, ModulationPerformanceTarget::TremoloRate);
  const auto panLfo = modulationEvents(track, ModulationPerformanceTarget::PanDepth);
  const auto reverb = events<ReverbPerformanceEvent>(track);
  const auto balance = events<StereoBalancePerformanceEvent>(track);
  const double minimum = 12.0 * std::log2(1.0 - 32.0 / 256.0);
  const double maximum = 12.0 * std::log2(1.0 + 32.0 / 128.0);

  expect(performance.diagnostics.empty() && vibrato.size() == 1 && vibrato.front()->context.pitchRangeSemitones &&
             std::abs(vibrato.front()->context.pitchRangeSemitones->minimum - minimum) < 0.000001 &&
             std::abs(vibrato.front()->context.pitchRangeSemitones->maximum - maximum) < 0.000001 &&
             vibratoRate.size() == 1 && vibratoRate.front()->context.frequencyHz &&
             std::abs(*vibratoRate.front()->context.frequencyHz - 8000.0 / (39.0 * 32.0 * 4.0)) < 0.000001 &&
             vibrato.front()->context.shape && vibrato.front()->context.shape->waveform == LfoWaveform::Sine &&
             vibrato.front()->context.polarity == LfoPolarity::Bipolar &&
             vibrato.front()->context.shape->samples.size() == 32 &&
             vibrato.front()->context.sampleImmediatelyOnNote &&
             vibrato.front()->context.restartMode == LfoRestartMode::PhaseAndDelay &&
             vibratoRate.front()->context.restartMode == LfoRestartMode::None,
         "vibrato should preserve the timer-0 oscillator and asymmetric driver pitch ratios");
  expect(tremolo.size() == 1 && tremolo.front()->context.polarity == LfoPolarity::Bipolar &&
             tremolo.front()->context.shape && tremolo.front()->context.initialPhaseCycles == 0.0 &&
             tremolo.front()->context.shape->waveform == LfoWaveform::Sine &&
             tremolo.front()->volumeDepthLinearGain == 0.25 && tremoloRate.size() == 1 &&
             tremoloRate.front()->context.frequencyHz &&
             std::abs(*tremoloRate.front()->context.frequencyHz - 8000.0 / (39.0 * 32.0 * 2.0)) < 0.000001 &&
             panLfo.size() == 1 && panLfo.front()->context.cyclesPerTick == 1.0 / 16.0 &&
             panLfo.front()->panDepth == 0.5,
         "tremolo mode bits and the sequence-clocked pan triangle should remain physical");
  expect(reverb.size() == 5 && reverb[1]->delayMilliseconds == 64.0 && reverb[1]->leftGain &&
             *reverb[1]->leftGain == 64.0 / 127.0 && reverb[2]->feedback == -0.5 && reverb[3]->voiceMask == 1 &&
             reverb[4]->voiceMask == 0,
         "EDL, signed EVOL/feedback, the shipped selector quirk, and echo voice masks should be retained");
  expect(!balance.empty() && balance.back()->leftGain == 0.5 && balance.back()->rightGain == 0.25,
         "the alternate mixer should clamp large coefficients while retaining smaller linear pan gains");
}

void lfoModesFollowTheDriverStateMachine() {
  const PerformanceSequence performance =
      render({0x19, 0, 1, 0x20, 0x19, 0, 2, 0x60, 0x19, 0, 1, 0xa0, 0x19, 0, 8, 0xe0, 0x1a, 0});
  const PerformanceSequence continuous = render({0x27, 0x85, 0x19, 0, 1, 0xe0, 0x27, 0x84, 0x30, 0});
  const PerformanceSequence portamento = render({0x19, 0, 1, 0x82, 0x25, 4, 0x30, 0x31, 0});
  const PerformanceSequence stoppedTremolo = render({0x1b, 0, 1, 0xa0, 0x1c, 0});
  const auto depth = modulationEvents(performance.tracks.front(), ModulationPerformanceTarget::VibratoDepth);
  const auto rate = modulationEvents(performance.tracks.front(), ModulationPerformanceTarget::VibratoRate);
  const auto continuousDepth = modulationEvents(continuous.tracks.front(), ModulationPerformanceTarget::VibratoDepth);
  const auto continuousDelay = events<VibratoDelayPerformanceEvent>(continuous.tracks.front());
  const auto portamentoDepth = modulationEvents(portamento.tracks.front(), ModulationPerformanceTarget::VibratoDepth);
  const auto portamentoNotes = events<NotePerformanceEvent>(portamento.tracks.front());
  const auto tremoloDepth = modulationEvents(stoppedTremolo.tracks.front(), ModulationPerformanceTarget::TremoloDepth);
  const MidiSequence portamentoMidi =
      renderMidiSequence(portamento, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto hasNonzeroBendAt = [&](u64 tick) {
    return std::ranges::any_of(portamentoMidi.tracks.front().events, [=](const MidiEvent& event) {
      const auto* bend = std::get_if<PitchBend>(&event);
      return bend != nullptr && bend->tick == tick && bend->value != 0;
    });
  };
  const double minimum = 12.0 * std::log2(1.0 - 32.0 / 256.0);
  const double maximum = 12.0 * std::log2(1.0 + 32.0 / 128.0);

  expect(performance.diagnostics.empty() && continuous.diagnostics.empty() && portamento.diagnostics.empty() &&
             stoppedTremolo.diagnostics.empty() && depth.size() == 5 && rate.size() == 4,
         "all encoded LFO phase modes should decode");
  expect(depth[0]->context.polarity == LfoPolarity::Positive && depth[0]->context.initialPhaseCycles == 0.0 &&
             depth[0]->context.pitchRangeSemitones && depth[0]->context.pitchRangeSemitones->minimum == 0.0 &&
             std::abs(depth[0]->context.pitchRangeSemitones->maximum - maximum) < 0.000001 &&
             rate[0]->context.frequencyHz &&
             std::abs(*rate[0]->context.frequencyHz - 8000.0 / (39.0 * 16.0)) < 0.000001 &&
             depth[0]->context.shape && depth[0]->context.shape->samples.size() == 16 &&
             depth[0]->context.shape->samples.at(8) == 1.0,
         "the positive-lobe mode should repeat after sixteen timer-0 steps");
  expect(depth[1]->context.polarity == LfoPolarity::Negative && depth[1]->context.initialPhaseCycles == 0.0 &&
             depth[1]->context.pitchRangeSemitones &&
             std::abs(depth[1]->context.pitchRangeSemitones->minimum - minimum) < 0.000001 &&
             depth[1]->context.pitchRangeSemitones->maximum == 0.0 && rate[1]->context.frequencyHz &&
             std::abs(*rate[1]->context.frequencyHz - 8000.0 / (39.0 * 16.0 * 2.0)) < 0.000001,
         "the negative-lobe mode should retain its signed phase and pitch range");
  expect(depth[2]->context.polarity == LfoPolarity::Bipolar && depth[2]->context.initialPhaseCycles == 0.0 &&
             depth[2]->context.pitchRangeSemitones &&
             std::abs(depth[2]->context.pitchRangeSemitones->minimum - minimum) < 0.000001 &&
             std::abs(depth[2]->context.pitchRangeSemitones->maximum - maximum) < 0.000001 &&
             rate[2]->context.frequencyHz &&
             std::abs(*rate[2]->context.frequencyHz - 8000.0 / (39.0 * 32.0)) < 0.000001 &&
             depth[3]->context.polarity == LfoPolarity::Bipolar && depth[3]->context.initialPhaseCycles == 0.0 &&
             depth[4]->pitchDepthSemitones == 0.0 && depth[4]->context.polarity == LfoPolarity::Bipolar &&
             depth[4]->context.zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote &&
             depth[4]->context.restartMode == LfoRestartMode::None && continuousDepth.size() == 1 &&
             continuousDepth.front()->context.initialPhaseCycles == 0.5 &&
             continuousDepth.front()->context.restartMode == LfoRestartMode::Phase &&
             continuousDepth.front()->context.noteRestartInitialPhaseCycles == 0.0 && continuousDelay.size() == 1 &&
             continuousDelay.front()->updateMode == LfoDelayUpdateMode::FutureNotesOnly &&
             portamentoDepth.size() == 1 && portamentoDepth.front()->context.shape &&
             portamentoDepth.front()->context.shape->samples.at(2) == 0.0 &&
             portamentoDepth.front()->context.shape->samples.at(3) > 0.0 && !hasNonzeroBendAt(1) &&
             hasNonzeroBendAt(2) &&
             portamentoNotes.size() == 2 && !portamentoNotes.back()->restartsEnvelope &&
             portamentoNotes.back()->restartsVibratoLfoPhase == true && !portamentoNotes.back()->restartsLfoPhase &&
             tremoloDepth.size() == 2 && tremoloDepth.back()->volumeDepthLinearGain == 0.0 &&
             tremoloDepth.back()->context.zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote,
         "the alternating mode should use both asymmetric lobes and retain the driver's reset behavior");
}

void trackAndMasterVolumeRetainIndependentResolution() {
  const PerformanceSequence performance = render({0x01, 0x80, 0x03, 0x80, 0x0c, 0x80, 0});
  const PerformanceTrack& track = performance.tracks.front();
  const auto levels = events<LevelPerformanceEvent>(track);
  const auto masters = events<MasterLevelPerformanceEvent>(track);
  const double half = 128.0 / 255.0;

  expect(performance.diagnostics.empty() && levels.size() == 3 && levels[0]->linearGain == 1.0 &&
             std::abs(levels[1]->linearGain - half) < 0.000001 &&
             std::abs(levels[2]->linearGain - half * half) < 0.000001 && levels[1]->sourceQuantization &&
             levels[1]->sourceQuantization->levels == 256 && levels[2]->sourceQuantization &&
             levels[2]->sourceQuantization->levels == 256,
         "track and channel volume should multiply independently at their full eight-bit precision");
  expect(masters.size() == 2 && std::abs(masters.front()->linearGain - 24.0 / 255.0) < 0.000001 &&
             std::abs(masters.back()->linearGain - half) < 0.000001,
         "master volume should be an absolute gain instead of a clipped boost over the $18 startup value");

  const MidiSequence midi = renderMidiSequence(performance);
  const auto volume = std::ranges::find_if(
      midi.tracks.front().events, [](const MidiEvent& event) { return std::holds_alternative<Volume14>(event); });
  const auto master =
      std::ranges::find_if(midi.tracks.front().events.rbegin(), midi.tracks.front().events.rend(),
                           [](const MidiEvent& event) { return std::holds_alternative<MasterVolume>(event); });
  expect(volume != midi.tracks.front().events.end(),
         "eight-bit source volume should automatically use a 14-bit MIDI controller");
  expect(master != midi.tracks.front().events.rend() &&
             std::get<MasterVolume>(*master).value == LevelScale::midi14FromLinear(half) &&
             std::get<MasterVolume>(*master).value < 0x3fff,
         "master volume should retain its level rather than saturating the 14-bit MIDI master controller");
}

void dynamicAdsrPitchAndControlFlowAreAudited() {
  const PerformanceSequence envelope = render({0x12, 0x2f, 0x13, 0x0e, 0x14, 0x0d, 0x15, 0x1a, 0x16, 0x11, 0xf0, 0});
  const auto envelopes = events<EnvelopePerformanceEvent>(envelope.tracks.front());
  const auto tuning = events<TuningPerformanceEvent>(envelope.tracks.front());
  expect(envelope.diagnostics.empty() && envelopes.size() == 5 &&
             envelopes[0]->update.fields == EnvelopeFields::Attack && envelopes[0]->update.values &&
             envelopes[0]->update.values->attackSeconds == snesDspAdsrAttackSeconds(0x0f) &&
             envelopes[3]->update.fields == EnvelopeFields::SecondDecay && envelopes[3]->update.values &&
             envelopes[3]->update.values->secondDecaySeconds == snesDspAdsrSustainSeconds(0x1a) &&
             !envelopes[4]->update.values && envelopes[4]->update.fields == EnvelopeFields::All,
         "dynamic ADSR should mask each real DSP field, including the full five-bit sustain rate");
  expect(tuning.size() == 1 && std::abs(tuning.front()->cents - 1200.0 * std::log2(1.0 - 16.0 / 2048.0)) < 0.000001,
         "fine tuning should use the driver's multiplicative pitch fraction");

  const PerformanceSequence pitchLimits = render({0x0b, 0xff, 0x17, 0x9c, 0x37, 8, 0x17, 0x00, 0x3f, 8, 0});
  const auto limitedNotes = events<NotePerformanceEvent>(pitchLimits.tracks.front());
  expect(pitchLimits.diagnostics.empty() && limitedNotes.size() == 2 && limitedNotes[0]->key == 51.0 &&
             limitedNotes[1]->key == 119.0,
         "note-base saturation should precede signed transpose and the DSP's 96-note pitch ceiling");

  const PerformanceSequence slide = render({0x37, 8, 0x29, 3, 2, 0x3f, 8, 0});
  const auto slideNotes = events<NotePerformanceEvent>(slide.tracks.front());
  const auto* slideIntent =
      slide.tracks.front().automations.empty() ? nullptr : pitchTransitionIntent(slide.tracks.front().automations[0]);
  expect(slide.diagnostics.empty() && slideNotes.size() == 2 && slideNotes[0]->durationTicks == 6 && slideIntent &&
             slideIntent->startKey == 25.0 && slideIntent->targetKey == 27.0 && slideIntent->timing.timelineTicks == 3,
         "literal lengths should use the two-tick gate subtraction and one-shot slides should target the next note");

  const PerformanceSequence portamento = render({0x25, 4, 0x37, 8, 0x3f, 8, 0x26, 0});
  const auto* glide = portamento.tracks.front().automations.empty()
                          ? nullptr
                          : pitchTransitionIntent(portamento.tracks.front().automations.front());
  expect(portamento.diagnostics.empty() && events<NotePerformanceEvent>(portamento.tracks.front()).size() == 2 &&
             glide && glide->previousNote && glide->startKey == 24.0 && glide->targetKey == 25.0 &&
             glide->timing.timelineTicks == 4,
         "persistent portamento should continue one voice and preserve its exact glide duration");

  const PerformanceSequence chained = render({0x29, 2, 2, 0x37, 8, 0x25, 4, 0x3f, 8, 0});
  const auto* chainedGlide = chained.tracks.front().automations.size() < 2
                                 ? nullptr
                                 : pitchTransitionIntent(chained.tracks.front().automations[1]);
  expect(
      chained.diagnostics.empty() && chainedGlide && chainedGlide->startKey == 26.0 && chainedGlide->targetKey == 25.0,
      "portamento should begin at the pitch reached by a preceding one-shot slide");

  const PerformanceSequence repeat = render({0x2a, 1, 0x37, 8, 0x2e, 0});
  expect(repeat.diagnostics.empty() && events<NotePerformanceEvent>(repeat.tracks.front()).size() == 2,
         "repeat counts should describe additional plays, matching the SPC700 repeat stack");
  const PerformanceSequence tie = render({0x37, 8, 0xf7, 8, 0});
  const auto tiedNotes = events<NotePerformanceEvent>(tie.tracks.front());
  expect(tie.diagnostics.empty() && tiedNotes.size() == 1 && tiedNotes.front()->durationTicks == 14,
         "ties should suppress retriggering and extend the prior gate with the driver's two-tick release margin");
}

void packedLengthPatternsFollowTheDriverByteOrder() {
  const PerformanceSequence performance = render({0x09, 0x94, 0x35, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0});
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  constexpr std::array<u32, 7> lengths{96, 72, 48, 32, 24, 12, 6};
  u64 tick = 0;
  expect(performance.diagnostics.empty() && notes.size() == lengths.size(),
         "packed note-length fixture should decode all seven selected master lengths");
  for (size_t i = 0; i < lengths.size(); ++i) {
    expect(notes[i]->header.tick == tick && notes[i]->durationTicks == lengths[i] - 2,
           "packed note-length masks should apply their second byte to the first half of the driver table");
    tick += lengths[i];
  }
}

}  // namespace

void runItikitiSnesModuleTests() {
  layoutAndSynthFollowRelocatedDriverTables();
  groupFallbackAndProgramBanksMatchTheDriver();
  physicalLfosAndMixerStateArePreserved();
  lfoModesFollowTheDriverStateMachine();
  trackAndMasterVolumeRetainIndependentResolution();
  dynamicAdsrPitchAndControlFlowAreAudited();
  packedLengthPatternsFollowTheDriverByteOrder();
}

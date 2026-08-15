/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HudsonSnes/HudsonSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::hudson_snes;

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

void appendLe16(std::vector<u8>& bytes, u32& cursor, u16 value) {
  writeLe16(bytes, cursor, value);
  cursor += 2;
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

void appendBytes(std::vector<u8>& bytes, u32& cursor, std::initializer_list<u8> values) {
  writeBytes(bytes, cursor, values);
  cursor += values.size();
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

template <class Timing>
std::vector<const PitchTransitionIntent*> pitchTransitions(const PerformanceTrack& track) {
  std::vector<const PitchTransitionIntent*> result;
  for (const PerformanceAutomation& automation : track.automations) {
    const auto* transition = pitchTransitionIntent(automation);
    if (transition != nullptr && std::holds_alternative<Timing>(transition->timing.physical)) {
      result.push_back(transition);
    }
  }
  return result;
}

std::vector<u32> runtimeData() {
  std::vector<u32> data(512);
  std::fill(data.begin() + 384, data.end(), 0xffffffffu);
  data[0] = 0x8fe080;
  return data;
}

PerformanceSequence render(Version version, u8 shift, bool velocity, std::vector<u8> bytes,
                           std::vector<u32> driverData = {}) {
  const SequenceDialect& dialect = sequenceDialect();
  if (driverData.empty()) {
    driverData = runtimeData();
  }
  SequenceProgram program{
      .runtime = sequenceRuntime(version, shift, velocity, std::move(driverData)),
      .timebase = dialect.timebase,
      .behavior = dialect.defaultBehavior,
      .tracks = {decodeSourceTrack(ByteReader(SourceId{151}, bytes), version, shift, velocity, 0, 0)},
  };
  program.behavior.initialTempoMicrosecondsPerQuarter = 512000;
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

std::vector<u8> v2ScannerFixture() {
  std::vector<u8> bytes(kAramSize);
  const std::array<u8, 35> engine{
      0xe5, 0x03, 0x08, 0xec, 0x04, 0x08, 0xda, 0x13, 0xe5, 0x05, 0x08, 0xec, 0x06, 0x08, 0xda, 0x15, 0xe5, 0x07,
      0x08, 0xec, 0x08, 0x08, 0xda, 0x17, 0xe5, 0x09, 0x08, 0xc5, 0x5b, 0x01, 0xe5, 0x0a, 0x08, 0xc4, 0x19,
  };
  std::ranges::copy(engine, bytes.begin() + 0x900);
  writeLe16(bytes, 0x0803, 0x0700);
  writeLe16(bytes, 0x0807, 0x5000);
  bytes[0x0809] = 0x60;
  writeLe16(bytes, 0x0700, 0x1000);
  writeLe16(bytes, 0x1000, 0x2000);
  std::ranges::copy(std::array<u8, 8>{0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x01}, bytes.begin() + 0x3000);

  u32 cursor = 0x2000;
  appendBytes(bytes, cursor, {0x03, 0x01, 0x00, 0x8f, 0xe0, 0x8a, 0x01, 0x01});
  appendLe16(bytes, cursor, 0x2100);
  appendBytes(bytes, cursor, {0x00});
  writeBytes(bytes, 0x2100, {0xd6, 7, 0xef, 2, 1, 0xe2, 64, 32, 0x83, 0xe9, 64, 32, 0x84, 0xfe, 0x0d, 5, 0xff});

  writeLe16(bytes, 0x40, 0x4000);
  writeLe16(bytes, 0x44, 0x4500);
  writeLe16(bytes, 0x46, 0x4600);
  writeLe16(bytes, 0x48, 0x4700);
  writeBytes(bytes, 0x4000 + 7 * 4, {0, 0x8f, 0xe0, 0x8a});
  writeLe16(bytes, 0x4500 + 2 * 2, 0x4800);
  writeBytes(bytes, 0x4800, {1, 32, 0xff});
  writeLe16(bytes, 0x4600 + 3 * 2, 0x4900);
  writeLe16(bytes, 0x4600 + 4 * 2, 0x4910);
  writeBytes(bytes, 0x4900, {0, 64, 0x80});
  writeBytes(bytes, 0x4910, {static_cast<u8>(-64), 0, 0x80});
  writeLe16(bytes, 0x4700 + 5 * 2, 0x4a00);
  bytes[0x4a00 + 24] = static_cast<u8>(-8);

  bytes[0x5000] = 0x01;
  bytes[0x5001] = 0x00;
  writeLe16(bytes, 0x6000, 0x6100);
  writeLe16(bytes, 0x6002, 0x6100);
  bytes[0x6100] = 0x01;
  return bytes;
}

void scannerBuildsACompleteV2Collection() {
  const std::vector<u8> bytes = v2ScannerFixture();
  const ByteReader reader(SourceId{150}, bytes);
  const auto layout = findLayout(reader);
  expect(layout && layout->version == Version::V2 && layout->sequenceHeaderAddress == 0x2000 &&
             layout->spcDirAddress == 0x6000 && layout->tuningTableAddress == 0x5000,
         "the 2.x engine structure should resolve the song list, tuning table, and DSP directory");
  const SequenceParse parsed = decodeSequence(reader, *layout, AssetId{159});
  const auto contains = [](const auto& recipes, u8 index) {
    return std::ranges::any_of(recipes, [=](const auto& recipe) { return recipe.index == index; });
  };
  expect(std::ranges::any_of(parsed.recipes.instruments, [](const InstrumentRow& row) { return row.program == 7; }) &&
             contains(parsed.recipes.pitchScripts, 2) && contains(parsed.recipes.customWaveforms, 3) &&
             contains(parsed.recipes.customWaveforms, 4) && contains(parsed.recipes.volumeCurves, 5),
         "sequence decoding should collect typed references to the driver's live resource tables");

  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "HudsonSnes fixture.aram"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection* collection = snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
  expect(snapshot.collections().size() == 1 && collection->members.sequence &&
             collection->members.instrumentSets.size() == 1 && collection->members.sampleCollections.size() == 1,
         "HudsonSnes scanning should publish a connected sequence, instrument set, and BRR sample collection");
  const auto* instruments = snapshot.asset<InstrumentSetAsset>(collection->members.instrumentSets.front());
  const Envelope* envelope =
      instruments != nullptr && !instruments->instruments.empty() && !instruments->instruments.front().regions.empty()
          ? &instruments->instruments.front().regions.front().envelope
          : nullptr;
  const Envelope expected = snesDspEnvelope(0x8f, 0xe0, 0x8a);
  expect(envelope != nullptr && envelope->releaseSeconds == expected.releaseSeconds,
         "Hudson instruments should use the curve-compensated native KOF release by default");
}

void earlyGateReleaseStateMachineMatchesSuperBomberman2() {
  std::vector<u32> instruments = runtimeData();
  instruments[0] = 0x8fe08a;
  const PerformanceSequence performance =
      render(Version::Early, 1, false, {0xd5, 8, 0x40, 3, 0x40, 3, 0xff}, std::move(instruments));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->header.tick == 0 &&
             notes[0]->durationTicks == 4 && notes[1]->header.tick == 6 && notes[1]->durationTicks == 4,
         "Hudson early quantize 8 should raise KOF on the penultimate driver tick");

  const Envelope envelope = driverEnvelope(0x8f, 0xe0, 0x8a);
  expect(envelope.decaySeconds == 0.0 && envelope.secondDecaySeconds && std::isinf(*envelope.secondDecaySeconds) &&
             envelope.releaseSeconds == snesDspEnvelope(0x8f, 0xe0, 0x8a).releaseSeconds &&
             std::abs(driverPseudoReleaseSeconds(0x8a) - 0.512) < 0.000001,
         "Super Bomberman 2's 8F E0 8A instrument should distinguish native KOF from its gated GAIN release");

  const PerformanceSequence shortened = render(
      Version::Early, 0, false, {0xd5, 4, 0x40, 8, 0xd5, 7, 0x40, 8, 0xd5, 0, 0x40, 8, 0xd5, 0x80, 0x40, 8, 0xff});
  const auto shortenedNotes = events<NotePerformanceEvent>(shortened.tracks.front());
  const auto envelopes = events<EnvelopePerformanceEvent>(shortened.tracks.front());
  expect(shortened.diagnostics.empty() && shortenedNotes.size() == 4 && shortenedNotes[0]->durationTicks == 5 &&
             shortenedNotes[1]->durationTicks == 7 && shortenedNotes[2]->durationTicks == 1 &&
             shortenedNotes[3]->durationTicks == 1 && envelopes.size() == 3,
         "shortened Hudson gates should use GAIN only when it precedes the independent native KOF tick");
}

void headerDecodesEveryVersionTwoRecipe() {
  std::vector<u8> bytes(kAramSize);
  u32 cursor = 0x100;
  appendBytes(
      bytes, cursor,
      {0x02, 0x01, 0x08, 0x01, 0x03, 0x01, 0x04, 0x00, 0x00, 0x01, 0x04, 0x01, 0x00, 0x24, 0x40, 0x0f, 0x05, 0x01});
  appendLe16(bytes, cursor, 0x300);
  appendBytes(bytes, cursor, {0x06, 0x01});
  appendLe16(bytes, cursor, 0x320);
  appendBytes(bytes, cursor, {0x09, 0x01});
  appendLe16(bytes, cursor, 0x360);
  appendBytes(bytes, cursor, {0x07, 0x00, 0x20, 0x20, 0x04, 0x40, 0x02, 0x04, 0x01, 0x04});
  appendLe16(bytes, cursor, 0x200);
  appendBytes(bytes, cursor, {0x00});
  writeBytes(bytes, 0x200, {0xfe, 0x0d, 0, 0x10, 6, 127, 0xff});
  writeBytes(bytes, 0x300, {1, 0x81, 0xfe, 2});
  writeLe16(bytes, 0x304, 0x300);
  writeBytes(bytes, 0x306, {0xff});
  writeBytes(bytes, 0x320, {0, 64, 127, 64, 0, 0x80});
  bytes[0x360 + 24] = static_cast<u8>(-16);
  writeBytes(bytes, 0x0844, {0, 0x8f, 0xe0, 0x80});

  const ByteReader reader(SourceId{152}, bytes);
  const auto header = parseHeader(reader, Version::V2, 0x100);
  expect(header && header->timebaseShift == 1 && header->noteVelocity && header->initialEchoLeft == 0x20 &&
             header->initialEchoRight == 0x20 && header->initialEchoDelay == 4 && header->initialEchoFeedback == 0x40 &&
             header->initialEchoFilter == 2 && header->initialEchoMask == 4 &&
             header->tracks == std::vector<std::pair<u8, u16>>{{2, 0x200}} && header->recipes.instruments.size() == 1 &&
             header->recipes.drums.size() == 1 && header->recipes.pitchScripts.size() == 1 &&
             header->recipes.pitchScripts.front().steps.size() == 3 && header->recipes.customWaveforms.size() == 1 &&
             header->recipes.customWaveforms.front().samples.size() == 5 && header->recipes.volumeCurves.size() == 1 &&
             header->recipes.volumeCurves.front().offsets[24] == -16,
         "Hudson 2.x headers should retain instruments, drums, pitch scripts, waveforms, echo, velocity, and tracks");

  const SequenceParse parsed = decodeSequence(
      reader, Layout{.version = Version::V2, .sequenceHeaderAddress = 0x100, .noteLengthTableAddress = 0x340},
      AssetId{155});
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
  const auto reverbs = events<ReverbPerformanceEvent>(performance.tracks.front());
  const auto levels = events<LevelPerformanceEvent>(performance.tracks.front());
  const ReverbPerformanceEvent* reverb = reverbs.empty() ? nullptr : reverbs.back();
  const std::string echoDetail = reverb == nullptr
                                     ? " (no reverb event)"
                                     : " (mask " + std::to_string(reverb->voiceMask.value_or(0)) + ", left " +
                                           std::to_string(reverb->leftGain.value_or(-9.0)) + ", delay " +
                                           std::to_string(reverb->delayMilliseconds.value_or(-9.0)) + ", feedback " +
                                           std::to_string(reverb->feedback.value_or(-9.0)) + ", filter " +
                                           std::to_string(reverb->filterIndex.value_or(0xff)) + ")";
  expect(reverb != nullptr && reverb->voiceMask == 4 && reverb->leftGain &&
             std::abs(*reverb->leftGain - 32.0 / 127.0) < 0.000001 && reverb->delayMilliseconds == 64.0 &&
             reverb->feedback == 0.5 && reverb->filterIndex == 2,
         "Hudson initial echo should preserve its DSP volume, delay, feedback, filter, and channel mask" + echoDetail);
  expect(!levels.empty() && std::abs(levels.back()->linearGain - 34.0 / 128.0) < 0.000001,
         "header 09 note-volume curves should apply the selected signed offset on each note");
}

void earlyHeaderGrammarSupportsBothInstrumentLengths() {
  std::vector<u8> bytes(kAramSize);
  u32 cursor = 0x100;
  appendBytes(bytes, cursor, {0x01});
  appendLe16(bytes, cursor, 0x200);
  appendBytes(bytes, cursor, {0x01, 0x02, 0x02, 0x04, 0x01, 0x8f, 0xe0, 0x8a, 0x03, 0x04, 0x00,
                              60,   64,   15,   0x04, 0x01, 0x02, 0x9f, 0xe1, 0x8b, 0x05, 0x01});
  appendLe16(bytes, cursor, 0x300);
  appendBytes(bytes, cursor, {0x00});
  writeBytes(bytes, 0x200, {0xff});
  writeBytes(bytes, 0x300, {1, 32, 0xff});

  const auto header = parseHeader(ByteReader(SourceId{156}, bytes), Version::Early, 0x100);
  expect(header && header->timebaseShift == 2 && header->tracks == std::vector<std::pair<u8, u16>>{{0, 0x200}} &&
             header->recipes.instruments.size() == 2 && header->recipes.instruments.front().srcn == 1 &&
             header->recipes.instruments.back().srcn == 2 && header->recipes.drums.size() == 1 &&
             header->recipes.pitchScripts.size() == 1,
         "early headers should share recipe decoding while preserving byte- and row-counted instrument commands");
}

void v2VolumeUsesThePostVelocityMixerCurve() {
  const PerformanceSequence relative = render(Version::V2, 2, false, {0xd9, 51, 0xdc, 0xf6, 0xff});
  const auto levels = events<LevelPerformanceEvent>(relative.tracks.front());
  expect(relative.diagnostics.empty() && levels.size() >= 2 &&
             std::abs(levels[levels.size() - 2]->linearGain - 26.0 / 128.0) < 0.000001 &&
             std::abs(levels.back()->linearGain - 14.0 / 128.0) < 0.000001,
         "Super Bomberman 5 DC F6 should map 51 -> 41 through the 2.x mixer curve (26 -> 14)");

  const PerformanceSequence velocity = render(Version::V2, 2, true, {0xd9, 51, 0x10, 6, 63, 0xff});
  const auto velocityLevels = events<LevelPerformanceEvent>(velocity.tracks.front());
  const auto notes = events<NotePerformanceEvent>(velocity.tracks.front());
  expect(velocity.diagnostics.empty() && !velocityLevels.empty() && notes.size() == 1 &&
             std::abs(notes.front()->linearVelocity - 0.5) < 0.000001 &&
             std::abs(velocityLevels.back()->linearGain * notes.front()->linearVelocity - 6.0 / 128.0) < 0.000001,
         "Hudson 2.x should apply velocity before its nonlinear mixer curve");
}

void v2PlaybackUsesAuditedTempoLfosAndDynamicAdsr() {
  const PerformanceSequence performance = render(
      Version::V2, 2, true, {0xd1, 120,  0xe2, 64, 32,   0,    0xe3, 2,    0xf2, 2,    0xfe, 0x1a, 0x0f, 0xfe, 0x1b,
                             7,    0xfe, 0x1c, 3,  0xfe, 0x1d, 0x12, 0xfe, 0x1e, 0x08, 0x10, 6,    63,   0xff});
  const auto tempos = events<TempoPerformanceEvent>(performance.tracks.front());
  const auto modulation = events<ModulationPerformanceEvent>(performance.tracks.front());
  const auto envelopes = events<EnvelopePerformanceEvent>(performance.tracks.front());
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto tremolo = std::ranges::find_if(modulation, [](const ModulationPerformanceEvent* event) {
    return event->target == ModulationPerformanceTarget::TremoloDepth && event->volumeDepthLinearGain;
  });
  expect(performance.diagnostics.empty() && !tempos.empty() && tempos.back()->microsecondsPerQuarter == 512000 &&
             notes.size() == 1 && notes.front()->durationTicks == 20 &&
             std::abs(notes.front()->linearVelocity - 0.5) < 0.000001 && envelopes.size() == 5 &&
             tremolo != modulation.end() && (*tremolo)->frequencyHz &&
             std::abs(*(*tremolo)->frequencyHz - 0.9765625) < 0.000001,
         "Hudson 2.x playback should use the timer-derived tempo, velocity, tremolo, and all dynamic envelope fields");
}

void conditionalDispatchAndEarlyOperandLayoutsMatchTheDriver() {
  const PerformanceSequence conditional =
      render(Version::V2, 2, true, {0xfe, 0x10, 0x00, 0x05, 0xfe, 0x12, 0x00, 0x05, 0xfe, 0x14,
                                    0x10, 0x00, 0x10, 0x01, 0x7f, 0xff, 0x20, 0x01, 0x7f, 0xff});
  const auto conditionalNotes = events<NotePerformanceEvent>(conditional.tracks.front());
  expect(conditional.diagnostics.empty() && conditionalNotes.size() == 1 && conditionalNotes.front()->key == 25.0,
         "subcommand 14 should jump when Z is set because the SPC dispatch BNE skips the embedded goto");

  const PerformanceSequence early = render(Version::Early, 0, false, {0xf1, 0x10, 0x01, 0xff});
  const auto earlyNotes = events<NotePerformanceEvent>(early.tracks.front());
  expect(early.diagnostics.empty() && earlyNotes.size() == 1 && earlyNotes.front()->durationTicks == 1,
         "early-driver F1 must remain operandless and a custom one-tick duration must not use table alternation");

  const PerformanceSequence directQuantize = render(Version::V2, 0, false, {0xd5, 0x80, 0x10, 1, 0xff});
  const auto directNotes = events<NotePerformanceEvent>(directQuantize.tracks.front());
  expect(directQuantize.diagnostics.empty() && directNotes.size() == 1 && directNotes.front()->durationTicks == 1,
         "direct quantize zero should preserve the driver's zero gate instead of inventing a 256-tick wrap");
}

void customPitchAttackAndPercussionPreserveDriverCurvesAndMixerRows() {
  std::vector<u32> customWave = runtimeData();
  customWave[128] = (512u << 16) | (static_cast<u32>(LfoWaveform::Sine) << 8) | 3;
  customWave.insert(customWave.end(), {static_cast<u8>(-64), 0, 64});
  const PerformanceSequence attack =
      render(Version::V2, 2, false, {0xe9, 64, 128, 0x80, 0x10, 6, 0xff}, std::move(customWave));
  const auto* transition = attack.tracks.front().automations.empty()
                               ? nullptr
                               : pitchTransitionIntent(attack.tracks.front().automations.front());
  const auto* curve = transition == nullptr ? nullptr : std::get_if<SampledAutomationCurve>(&transition->curve);
  expect(attack.diagnostics.empty() && transition != nullptr && curve != nullptr && curve->samples.size() >= 3 &&
             transition->preferredRendering == PitchTransitionRenderingHint::PitchBend &&
             std::abs(curve->samples.front().value - (24.0 - 32.0 / 127.0)) < 0.000001 &&
             std::abs(curve->samples.back().value - (24.0 + 32.0 / 127.0)) < 0.000001,
         "custom Hudson pitch attacks should retain the driver's sampled waveform and physical pitch curve");

  std::vector<u32> drums = runtimeData();
  drums[384 + 24] = (0u << 24) | (60u << 16) | (32u << 8) | 0;
  const PerformanceSequence percussion = render(Version::V2, 2, false, {0xfe, 0x03, 0x10, 6, 0xff}, std::move(drums));
  const auto levels = events<LevelPerformanceEvent>(percussion.tracks.front());
  const auto balances = events<StereoBalancePerformanceEvent>(percussion.tracks.front());
  expect(percussion.diagnostics.empty() && !levels.empty() && !balances.empty() &&
             std::abs(levels.back()->linearGain - 9.0 / 128.0) < 0.000001 && balances.back()->leftGain == 0.0 &&
             balances.back()->rightGain == 1.0,
         "percussion notes should replace track volume and pan with the selected drum row, as the driver does");
}

void v1MixerAndPitchPipelineMatchesSuperBomberman3() {
  std::vector<u32> drums = runtimeData();
  drums[384 + 24] = (60u << 16) | (0x80u << 8) | 15;
  const PerformanceSequence percussion = render(Version::V1, 2, false, {0xfe, 0x03, 0x10, 6, 0xff}, std::move(drums));
  const auto levels = events<LevelPerformanceEvent>(percussion.tracks.front());
  const auto balances = events<StereoBalancePerformanceEvent>(percussion.tracks.front());
  expect(percussion.diagnostics.empty() && !levels.empty() && !balances.empty() &&
             std::abs(levels.back()->linearGain - 128.0 / 255.0) < 0.000001 &&
             std::abs(levels.back()->linearGain * balances.back()->leftGain - 44.0 / 127.0) < 0.000001,
         "Hudson 1.x drums should retain all eight volume bits and both SPC700 mixer truncations");

  const PerformanceSequence pitched =
      render(Version::V1, 2, false, {0xe2, 12, 6, 0xe3, 1, 0xe9, 127, 68, 1, 0xe9, 0, 0, 0, 0x18, 6, 0x20, 6, 0xff});
  const auto notes = events<NotePerformanceEvent>(pitched.tracks.front());
  const auto modulation = events<ModulationPerformanceEvent>(pitched.tracks.front());
  const ModulationPerformanceEvent* vibrato = nullptr;
  for (const auto* event : modulation) {
    if (event->target == ModulationPerformanceTarget::VibratoDepth && event->pitchRangeSemitones &&
        event->pitchDepthSemitones && *event->pitchDepthSemitones > 0.4) {
      vibrato = event;
      break;
    }
  }
  const auto attacks = pitchTransitions<FixedDurationPitchSlideTiming>(pitched.tracks.front());
  const auto* attack = attacks.empty() ? nullptr : attacks.back();
  const auto* timing =
      attack == nullptr ? nullptr : std::get_if<FixedDurationPitchSlideTiming>(&attack->timing.physical);
  expect(pitched.diagnostics.empty() && notes.size() == 2 && !notes.back()->restartsLfoPhase &&
             attacks.size() == 1 && timing != nullptr && timing->milliseconds == 508.0 &&
             attack->targetKey - attack->startKey > 10.0 && vibrato != nullptr && vibrato->delayTicks == 0 &&
             vibrato->pitchRangeSemitones->minimum < 0.0 && vibrato->pitchRangeSemitones->maximum > 0.0,
         "Hudson 1.x should retain pitch state across slurs and express raw DSP pitch envelopes and vibrato");
}

void pitchScriptsUseDriverDefaultsAndZeroMeans256Ticks() {
  std::vector<u32> script = runtimeData();
  script[256] = (512u << 8) | 1;
  script.push_back(127);
  const PerformanceSequence performance = render(Version::V2, 2, false, {0xef, 0, 1, 0x10, 6, 0xff}, std::move(script));
  const auto* transition = performance.tracks.front().automations.empty()
                               ? nullptr
                               : pitchTransitionIntent(performance.tracks.front().automations.front());
  expect(performance.diagnostics.empty() && transition != nullptr && transition->timing.timelineTicks == 1024 &&
             std::abs(transition->targetKey - (24.0 + 2.0 * 127.0 / 128.0)) < 0.000001,
         "pitch scripts should start at the driver's two-semitone range and treat a zero duration as 256 ticks");
}

void reversePhasePreservesSignedStereoGains() {
  const PerformanceSequence performance = render(Version::V2, 2, false, {0xda, 15, 0xdb, 3, 0x10, 6, 0xff});
  const auto balances = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && !balances.empty() && balances.back()->leftGain < 0.0 &&
             balances.back()->rightGain < 0.0,
         "reverse-phase commands should retain signed left and right channel gains");
}

void periodicVolumeSlidesRunOnTheDriverClock() {
  const PerformanceSequence performance = render(Version::V2, 2, false, {0xd9, 40, 0xf3, 8, 0x00, 24, 0xff});
  const auto levels = events<LevelPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && levels.size() >= 5 &&
             std::abs(levels.back()->linearGain - 17.0 / 128.0) < 0.000001,
         "periodic volume slides should accumulate eighth-steps at the driver's timebase-adjusted interval");
}

void portamentoRetainsPhysicalDriverTiming() {
  const PerformanceSequence performance = render(Version::V2, 2, false, {0x10, 6, 0xf1, 126, 0, 0x20, 6, 0xff});
  const auto transitions = pitchTransitions<FixedRatePitchSlideTiming>(performance.tracks.front());
  const auto* pitch = transitions.empty() ? nullptr : transitions.front();
  const auto* timing = pitch == nullptr ? nullptr : std::get_if<FixedRatePitchSlideTiming>(&pitch->timing.physical);
  expect(performance.diagnostics.empty() && pitch != nullptr && timing != nullptr &&
             pitch->preferredRendering == PitchTransitionRenderingHint::Portamento &&
             timing->semitonesPerSecond == 250.0,
         "portamento should retain the driver's fixed physical pitch rate and native-portamento preference");

  const PerformanceSequence v1 = render(Version::V1, 2, false, {0xf1, 1, 0, 0x10, 6, 0x30, 6, 0xff});
  const auto v1Transitions = pitchTransitions<FixedDurationPitchSlideTiming>(v1.tracks.front());
  const auto* v1Pitch = v1Transitions.empty() ? nullptr : v1Transitions.front();
  const auto* v1Timing =
      v1Pitch == nullptr ? nullptr : std::get_if<FixedDurationPitchSlideTiming>(&v1Pitch->timing.physical);
  expect(v1.diagnostics.empty() && v1Timing != nullptr && v1Timing->milliseconds == 20.0,
         "Hudson 1.x portamento should advance raw DSP pitch every four milliseconds after its anchor note");
}

void optionalRealCorpusSmokeTest() {
  const char* corpus = std::getenv("VGMTRANS_HUDSON_SNES_CORPUS");
  if (corpus == nullptr || !std::filesystem::is_directory(corpus)) {
    return;
  }
  u32 files = 0;
  u32 recognized = 0;
  u32 decoded = 0;
  u32 rendered = 0;
  u32 clean = 0;
  u32 unsupported = 0;
  u32 synthConnected = 0;
  std::map<std::string, std::pair<u32, u32>> games;
  std::map<std::string, u32> synthGames;
  std::map<std::string, u32> diagnosticCounts;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(corpus)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".spc") {
      continue;
    }
    std::ifstream stream(entry.path(), std::ios::binary);
    std::vector<u8> file{std::istreambuf_iterator<char>(stream), {}};
    if (file.size() < 0x10100) {
      continue;
    }
    std::vector<u8> aram(file.begin() + 0x100, file.begin() + 0x10100);
    constexpr std::array<u8, 8> signature{0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x01};
    if (std::search(aram.begin(), aram.end(), signature.begin(), signature.end()) == aram.end()) {
      continue;
    }
    ++files;
    ++games[entry.path().parent_path().filename().string()].second;
    const ByteReader reader(SourceId{153}, aram);
    const auto layout = findLayout(reader);
    const bool found = layout.has_value();
    recognized += found;
    games[entry.path().parent_path().filename().string()].first += found;
    if (layout) {
      std::vector<Diagnostic> diagnostics;
      const SequenceParse parsed = decodeSequence(reader, *layout, AssetId{154 + files}, nullptr, &diagnostics);
      decoded += !parsed.program.tracks.empty();
      const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
      rendered += !performance.tracks.empty();
      clean += diagnostics.empty() && performance.diagnostics.empty();
      unsupported += sequenceUsesSemantic(parsed.program, SequenceSemantic::Unsupported);
      Session session;
      session.registerFormat(module());
      session.addSource(SourceFile{.name = entry.path().filename().string()}, aram);
      session.scanPendingSources();
      const SessionSnapshot snapshot = session.snapshot();
      if (!snapshot.collections().empty()) {
        const CollectionMembers& members = snapshot.collections().front().members;
        const bool connected =
            members.sequence && !members.instrumentSets.empty() && !members.sampleCollections.empty();
        synthConnected += connected;
        synthGames[entry.path().parent_path().filename().string()] += connected;
      }
      for (const Diagnostic& diagnostic : diagnostics) {
        ++diagnosticCounts[diagnostic.code + ":" + diagnostic.message];
      }
      for (const Diagnostic& diagnostic : performance.diagnostics) {
        ++diagnosticCounts[diagnostic.code + ":" + diagnostic.message];
      }
    }
  }
  std::string detail;
  for (const auto& [game, count] : games) {
    detail += " " + game + "=" + std::to_string(count.first) + "/" + std::to_string(count.second) + "/" +
              std::to_string(synthGames[game]);
  }
  for (const auto& [diagnostic, count] : diagnosticCounts) {
    detail += " [" + std::to_string(count) + "x " + diagnostic + "]";
  }
  expect(files != 0 && recognized * 10 >= files * 9 && decoded == recognized && rendered == recognized &&
             clean * 10 >= recognized * 9 && synthConnected == recognized && unsupported == 0,
         "at least 90% of the explicitly supplied Hudson SPC corpus should pass structural discovery (" +
             std::to_string(recognized) + "/" + std::to_string(files) + ", decoded " + std::to_string(decoded) +
             ", rendered " + std::to_string(rendered) + ", clean " + std::to_string(clean) + ", synth-connected " +
             std::to_string(synthConnected) + ", unsupported " + std::to_string(unsupported) + "):" + detail);
}

}  // namespace

void runHudsonSnesModuleTests() {
  scannerBuildsACompleteV2Collection();
  earlyGateReleaseStateMachineMatchesSuperBomberman2();
  headerDecodesEveryVersionTwoRecipe();
  earlyHeaderGrammarSupportsBothInstrumentLengths();
  v2VolumeUsesThePostVelocityMixerCurve();
  v2PlaybackUsesAuditedTempoLfosAndDynamicAdsr();
  conditionalDispatchAndEarlyOperandLayoutsMatchTheDriver();
  customPitchAttackAndPercussionPreserveDriverCurvesAndMixerRows();
  v1MixerAndPitchPipelineMatchesSuperBomberman3();
  pitchScriptsUseDriverDefaultsAndZeroMeans256Ticks();
  reversePhasePreservesSignedStereoGains();
  periodicVolumeSlidesRunOnTheDriverClock();
  portamentoRetainsPhysicalDriverTiming();
  optionalRealCorpusSmokeTest();
}

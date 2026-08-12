/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatSnes/HeartBeatSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::heartbeat_snes;

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

template <class Range>
void writeBytes(std::vector<u8>& bytes, u32 offset, const Range& values) {
  std::ranges::copy(values, bytes.begin() + offset);
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

constexpr std::array<u8, 40> kLengthsDq3{
    0, 0, 1, 7, 1, 2, 3, 1, 0, 1, 2, 1, 0, 1, 1, 3, 0, 1, 2, 3,
    3, 3, 0, 1, 2, 3, 0, 0, 0, 8, 2, 1, 2, 2, 0, 0, 0, 1, 0, 1,
};
constexpr std::array<u8, 40> kLengthsDq6{
    0, 0, 1, 7, 1, 2, 3, 1, 0, 1, 2, 1, 2, 1, 1, 3, 0, 1, 2, 3,
    3, 3, 0, 1, 2, 3, 3, 0, 0, 8, 2, 1, 2, 2, 0, 0, 0, 1, 0, 1,
};

std::vector<u8> scannerFixture(Version version) {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x0100,
             {0xee, 0xf6, 0x00, 0x70, 0xc4, 0x10, 0xf6, 0x04, 0x70, 0xc4, 0x11, 0xf8, 0x12, 0xdd, 0xd5, 0x34, 0x12,
              0x8d, 0x00});
  writeBytes(bytes, 0x0200,
             {0xf5, 0x00, 0x00, 0x28, 0x0f, 0xfd, 0xf6, 0x00, 0x00, 0xc4, 0x20, 0xf6, 0x00, 0x00, 0xc4, 0x21});
  writeBytes(bytes, 0x0300, {0xe8, 0x60, 0x8d, 0x5d, 0x3f, 0x00, 0x00, 0xe8, 0x00});
  writeBytes(bytes, 0x0400, {0x3f, 0x00, 0x00, 0x8d, 0x06, 0xcf, 0xfd, 0x6d, 0xf7, 0x08, 0xd5, 0x00, 0x00,
                             0xeb, 0x36, 0xf6, 0x00, 0x00, 0x28, 0x0f, 0x8d, 0x10, 0xcf, 0xee, 0x60, 0x97,
                             0x08, 0xfc, 0x6d, 0xfd, 0xf6, 0x00, 0x71, 0x8d, 0x04, 0x3f, 0x00, 0x00, 0xee});
  writeBytes(bytes, 0x0500, version == Version::DragonQuest3 ? kLengthsDq3 : kLengthsDq6);

  writeLe16(bytes, 0x20, 0x2000);
  writeLe16(bytes, 0x2000, 0x0100);
  writeLe16(bytes, 0x2002, 0x1000);
  writeLe16(bytes, 0x2004, 0);
  writeBytes(bytes, 0x3000, {0xd4, 0x02, 0x08, 0x7f, 0x80, 0x00});

  // Program 2 selects song-local sample 1. Song index 1 changes that to table
  // index 17, which resolves to SRCN 3.
  // Intermezzo uses the same 04 00 tuning scalar on its formerly silent part.
  writeBytes(bytes, 0x210c, {0x01, 0x0f, 0xe5, 0x00, 0x04, 0x00});
  // Two music entries, a terminator, then one resident SFX entry. The active
  // music is entry 1; this is the split encoded by the real driver tables.
  writeBytes(bytes, 0x7000, {0x00, 0x00, 0x00, 0x00, 0x23, 0x20, 0x00, 0x22});
  writeLe16(bytes, 0x2300, 0x0100);
  writeLe16(bytes, 0x2302, 0x0018);
  writeLe16(bytes, 0x2304, 0);
  bytes[0x2318] = 0;
  bytes[0x1234] = 1;
  bytes[0x7111] = 3;
  writeLe16(bytes, 0x600c, 0x6200);
  writeLe16(bytes, 0x600e, 0x6200);
  bytes[0x6200] = 0x01;
  return bytes;
}

PerformanceSequence render(Version version, std::vector<u8> bytes) {
  const SequenceDialect& dialect = sequenceDialect();
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .behavior = dialect.defaultBehavior,
      .tracks = {decodeSourceTrack(ByteReader(SourceId{181}, bytes), version, 0, 0, 0)},
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
}

void layoutsRecoverVersionedDriverState() {
  for (const Version version : {Version::DragonQuest3, Version::DragonQuest6}) {
    const std::vector<u8> bytes = scannerFixture(version);
    const auto layout = findLayout(ByteReader(SourceId{182}, bytes));
    expect(layout && layout->version == version && layout->sequenceHeaderAddress == 0x2000 &&
               layout->instrumentTableAddress == 0x2100 && layout->spcDirAddress == 0x6000 &&
               layout->srcnTableAddress == 0x7100 && layout->songIndex == 1,
           "HeartBeatSnes signatures should recover the active song and all synth tables");
    const SequenceParse parsed = decodeSequence(ByteReader(SourceId{183}, bytes), *layout, AssetId{183});
    expect(parsed.program.tracks.size() == 1 && parsed.headerRange.size == 6 && parsed.recipes.programs.contains(2),
           "sequence decoding should follow relative track pointers and collect referenced programs");
  }

  std::vector<u8> sfx = scannerFixture(Version::DragonQuest3);
  writeLe16(sfx, 0x20, 0x2200);
  writeLe16(sfx, 0x2200, 0x0006);
  writeLe16(sfx, 0x2202, 0x0018);
  writeLe16(sfx, 0x2204, 0);
  // The short resident SFX loops from $222b to $2225, just like the transient
  // sequence that happened to be selected in the Town snapshot.
  writeBytes(sfx, 0x2218,
             {0xdd, 0x30, 0xdb, 0xa0, 0xd4, 0x00, 0xe3, 0xdc, 0xe0, 0xf5, 0xf9, 0x07, 0x1e, 0x02, 0x7d,
              0xc0, 0x01, 0x75, 0xc0, 0xf2, 0x25, 0x00});
  sfx[0x1234] = 0;
  const auto musicDuringSfx = findLayout(ByteReader(SourceId{184}, sfx));
  expect(musicDuringSfx && musicDuringSfx->sequenceHeaderAddress == 0x2300 && musicDuringSfx->songIndex == 0,
         "a resident SFX work pointer must not replace the persistent active music sequence");
}

void scannerBuildsAuditedDynamicInstruments() {
  const std::vector<u8> bytes = scannerFixture(Version::DragonQuest3);
  Session session;
  session.registerFormat(definition());
  session.addSource(SourceFile{.name = "HeartBeatSnes fixture.aram"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection* collection = snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
  expect(snapshot.collections().size() == 1 && collection->members.sequence &&
             collection->members.instrumentSets.size() == 1 && collection->members.sampleCollections.size() == 1,
         "HeartBeatSnes scanning should publish a complete sequence, instrument, and BRR collection");

  const auto* set = snapshot.asset<InstrumentSetAsset>(collection->members.instrumentSets.front());
  const Instrument* instrument = set != nullptr && set->instruments.size() == 1 ? &set->instruments.front() : nullptr;
  const Region* region =
      instrument != nullptr && instrument->regions.size() == 1 ? &instrument->regions.front() : nullptr;
  const double expectedUnity = 72.0 - std::log2(4.0 * 0x10d4 / 4096.0) * 12.0;
  expect(instrument != nullptr && instrument->identity && instrument->identity->key == 2 && region != nullptr &&
             std::abs(region->unityKey - expectedUnity) < 0.000001 && region->envelope.secondDecaySeconds &&
             region->envelope.releaseSeconds == snesDspAdsrSustainSeconds(5),
         "instrument rows should use the multiply routine's 8.8 tuning and the driver's ADSR2 gate-release rate");
}

void notesMixingAndVersionedOpcodesMatchTheDriver() {
  const PerformanceSequence dq3 =
      render(Version::DragonQuest3, {0xde, 0xe3, 0x80, 0x08, 0x7f, 0x80, 0xd2, 0x82, 0xd3, 0xec, 0x00});
  const auto notes = events<NotePerformanceEvent>(dq3.tracks.front());
  const auto levels = events<LevelPerformanceEvent>(dq3.tracks.front());
  const auto instruments = events<InstrumentPerformanceEvent>(dq3.tracks.front());
  const auto* legatoTransition =
      dq3.tracks.front().automations.empty() ? nullptr : pitchTransitionIntent(dq3.tracks.front().automations.front());
  const double defaultMaster = std::pow(0xc0 / 255.0, 2.0);
  expect(dq3.diagnostics.empty() && notes.size() == 2 && notes[0]->durationTicks == 7 && notes[1]->durationTicks == 8 &&
             legatoTransition != nullptr && legatoTransition->previousNote &&
             legatoTransition->timing.timelineTicks == 0 && levels.size() == 2 &&
             std::abs(levels.back()->linearGain - defaultMaster * std::pow(0x80 / 255.0, 2.0)) < 0.000001 &&
             instruments.size() == 1,
         "DQ3 PMON opcodes, exact gate math, attack-free legato, and squared mixer gain should coexist");

  const PerformanceSequence dq6 =
      render(Version::DragonQuest6, {0xde, 0xaa, 0xbb, 0x08, 0x7f, 0x80, 0xec, 0x01, 0x02, 0x03, 0x00});
  expect(dq6.diagnostics.empty() && events<NotePerformanceEvent>(dq6.tracks.front()).size() == 1,
         "DQ6 should consume the reserved DE/EC operands instead of treating them as DQ3 PMON toggles");

  const PerformanceSequence program = render(Version::DragonQuest3, {0xd4, 3, 0x00});
  const auto envelopes = events<EnvelopePerformanceEvent>(program.tracks.front());
  expect(envelopes.size() == 1 && !envelopes.front()->update.values &&
             envelopes.front()->update.fields == EnvelopeFields::All &&
             envelopes.front()->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks,
         "program changes should restore the complete patch envelope on active and future voices");
}

void modulationPitchAndEchoRemainPhysical() {
  const PerformanceSequence modulation =
      render(Version::DragonQuest3, {0xd8, 2, 0x40, 0x80, 0xd9, 4, 0xe1, 3, 0x20, 0x80, 0x08, 0x7f, 0x80, 0x00});
  const auto vibratoDepth = modulationEvents(modulation.tracks.front(), ModulationPerformanceTarget::VibratoDepth);
  const auto vibratoRate = modulationEvents(modulation.tracks.front(), ModulationPerformanceTarget::VibratoRate);
  const auto tremoloDepth = modulationEvents(modulation.tracks.front(), ModulationPerformanceTarget::TremoloDepth);
  const auto tremoloRate = modulationEvents(modulation.tracks.front(), ModulationPerformanceTarget::TremoloRate);
  const double expectedTremolo = 1.0 - std::pow(128.0 / 255.0, 2.0);
  expect(modulation.diagnostics.empty() && !vibratoDepth.empty() && !vibratoRate.empty() && !tremoloDepth.empty() &&
             !tremoloRate.empty() && std::abs(*vibratoDepth.front()->pitchDepthSemitones - 127.0 / 256.0) < 0.000001 &&
             std::abs(*vibratoRate.front()->cyclesPerTick - 0.25) < 0.000001 &&
             std::abs(*tremoloDepth.front()->volumeDepthLinearGain - expectedTremolo) < 0.000001 &&
             std::abs(*tremoloRate.front()->cyclesPerTick - 0.125) < 0.000001 &&
             tremoloDepth.front()->tremoloGainMode == TremoloGainMode::NoBoost,
         "vibrato and tremolo should retain their audited triangle rate, depth, delay, and attenuation polarity");

  const PerformanceSequence pitch =
      render(Version::DragonQuest3, {0x08, 0x7f, 0x80, 0xe5, 1, 4, 0x84, 0xe6, 2, 3, 5, 0x84, 0x00});
  expect(pitch.diagnostics.empty() && pitch.tracks.front().automations.size() == 2,
         "immediate E5 slides and persistent E6 envelopes should both produce pitch transitions");
  const auto* inlineSlide = pitchTransitionIntent(pitch.tracks.front().automations[0]);
  const auto* persistent = pitchTransitionIntent(pitch.tracks.front().automations[1]);
  expect(inlineSlide != nullptr && inlineSlide->startKey == 0.0 && inlineSlide->targetKey == 4.0 &&
             inlineSlide->timing.timelineTicks == 4 && pitch.tracks.front().automations[0].realization.startTick == 1 &&
             persistent != nullptr && persistent->startKey == 4.0 && persistent->targetKey == 9.0 &&
             persistent->timing.timelineTicks == 3,
         "pitch transitions should preserve source keys, delays, durations, and signed envelope depth");

  const PerformanceSequence echo = render(
      Version::DragonQuest3,
      {0xea, 0x40, 0xc0, 0xeb, 5, 0x80, 3, 0xee, 0xef, 0x34, 0x33, 0x00, 0xd9, 0xe5, 0x01, 0xfc, 0xeb, 0xed, 0x00});
  const auto reverb = events<ReverbPerformanceEvent>(echo.tracks.front());
  expect(echo.diagnostics.empty() && reverb.size() == 6,
         "initial echo state and every echo command should emit portable reverb events");
  expect(reverb[2]->delayMilliseconds == 80.0 && reverb[2]->feedback == -1.0,
         "echo delay and signed feedback should match the DSP registers");
  expect(reverb[3]->voiceMask == 1 && reverb[4]->filterIndex == 3 && reverb[5]->voiceMask == 0,
         "echo commands should preserve signed stereo levels, feedback, delay, voice mask, and FIR identity");
}

void dynamicAdsrAndLoopsUseAuditedFieldSemantics() {
  const PerformanceSequence performance =
      render(Version::DragonQuest3, {0xf0, 0x6f, 0xa7, 0xf9, 0x03, 0x05, 0xf9, 0x06, 0x1a, 0xf9, 0x07, 0x1b, 0x00});
  const auto envelopes = events<EnvelopePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && envelopes.size() == 4,
         "all complete and partial ADSR commands should emit envelope events");
  expect(envelopes[0]->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks && envelopes[0]->update.values,
         "F0 should immediately replace the active and future envelope");
  expect(envelopes[0]->update.values->secondDecaySeconds == snesDspEnvelope(0xef, 0xa7, 0).secondDecaySeconds,
         "F0 should install ADSR2 SR as the held sustain rate from the selected sustain level");
  expect(envelopes[0]->update.values->releaseSeconds == snesDspAdsrSustainSeconds(7),
         "F0 should also initialize the separate gate-release rate from ADSR2 SR");
  expect(envelopes[2]->update.fields == EnvelopeFields::SecondDecay && envelopes[2]->update.values &&
             envelopes[2]->update.values->secondDecaySeconds == snesDspAdsrSustainSeconds(0x1a) &&
             envelopes[3]->update.fields == EnvelopeFields::Release && envelopes[3]->update.values &&
             envelopes[3]->update.values->releaseSeconds == snesDspAdsrSustainSeconds(0x1b),
         "F0 should update active ADSR while extended 06/07 distinguish held sustain from gate release");

  // Repeat count 3 makes the conditional branch play the body three times.
  const PerformanceSequence loop =
      render(Version::DragonQuest3, {0xf9, 0x00, 3, 0x04, 0x7f, 0x80, 0xf9, 0x01, 3, 0, 0x00});
  expect(loop.diagnostics.empty() && events<NotePerformanceEvent>(loop.tracks.front()).size() == 3,
         "the extended repeat counter should decrement and branch exactly like the SPC700 handler");
}

}  // namespace

void runHeartBeatSnesModuleTests() {
  layoutsRecoverVersionedDriverState();
  scannerBuildsAuditedDynamicInstruments();
  notesMixingAndVersionedOpcodesMatchTheDriver();
  modulationPitchAndEchoRemainPhysical();
  dynamicAdsrAndLoopsUseAuditedFieldSemantics();
}

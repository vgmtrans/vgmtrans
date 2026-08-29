/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NeverlandSnes/NeverlandSnes.h"

#include "ValueFormatTestSupport.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::neverland_snes;

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

Layout runtimeLayout(Version version = Version::Modern) {
  Layout layout{
      .version = version,
      .sequenceBaseAddress = 0,
      .instrumentTableAddress = 0x300,
      .spcDirAddress = 0x400,
      .initialTempo = 0x40,
      .initialMasterVolume = version == Version::Original ? u8{0x7f} : u8{0x70},
      .initialEchoDelay = version == Version::Modern ? u8{2} : u8{4},
  };
  layout.tracks[0] = TrackLayout{.active = true, .playlistAddress = 0};
  return layout;
}

PerformanceSequence render(std::vector<u8> bytes, Layout layout = runtimeLayout()) {
  bytes.resize(std::max<size_t>(bytes.size(), 0x500));
  writeBytes(bytes, layout.instrumentTableAddress, {0x8f, 0xe0, 0x00, 0x10});
  const ByteReader reader{SourceId{301}, bytes};
  const SequenceProgramConfig config = sequenceConfig(layout);
  SequenceProgram program{
      .runtime = sequenceRuntime(reader, layout),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {decodeSourceTrack(reader, layout, 0, 0)},
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

std::vector<u8> scannerFixture() {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x0100, {0x8f, 0x10, 0x18, 0x8f, 0x30, 0x19, 0xcd, 0x00, 0x8d, 0x00, 0xf4,
                             0x3d, 0x28, 0x02, 0xd4, 0x3d, 0xf7, 0x18, 0x68, 0xff, 0xf0, 0x08});
  writeBytes(bytes, 0x0200,
             {0x1c, 0x1c, 0xfd, 0xf6, 0x00, 0x20, 0xf6, 0x01, 0x20, 0xf6, 0x02, 0x20, 0xf6, 0x03, 0x20});
  writeBytes(bytes, 0x0300, {0x5d, 0x40, 0x4d, 0x00, 0x0d, 0x00, 0x3c, 0x00, 0x2c, 0x00, 0xff});
  writeBytes(bytes, 0x0500,
             {0x8f, 0x6c, 0xf2, 0xe8, 0x20, 0xc4, 0xf3, 0xc5, 0xda, 0x08, 0xe8, 0x78, 0xc5, 0xcf, 0x08,
              0x3f, 0x5d, 0x29, 0xe8, 0x1e, 0x3f, 0xe8, 0x22, 0xe8, 0x5a, 0xc5, 0xd4, 0x08, 0xe8, 0x02,
              0xc5, 0xd3, 0x08});

  writeBytes(bytes, 0x3000, {'S', '2', 'C', 0x52});
  std::fill(bytes.begin() + 0x3010, bytes.begin() + 0x3018, 0xff);
  bytes[0x3010] = 0;
  bytes[0x3019] = 5;
  writeLe16(bytes, 0x3020, 0x0100);
  writeBytes(bytes, 0x3100, {0x02, 0x00, 0xff});
  writeBytes(bytes, 0x3200, {0x3c, 4, 3, 0x7f, 0xfd});

  writeBytes(bytes, 0x2000, {0x8f, 0xe0, 0x02, 0xf0});
  writeLe16(bytes, 0x4000, 0x5000);
  writeLe16(bytes, 0x4002, 0x5000);
  writeBytes(bytes, 0x5000, {1, 0, 0, 0, 0, 0, 0, 0, 0});
  return bytes;
}

void relocatedLayoutUsesDriverCodeAndHeaderContracts() {
  const std::vector<u8> fixture = scannerFixture();
  const auto layout = findLayout(ByteReader{SourceId{302}, fixture});
  expect(layout && layout->version == Version::Modern && layout->sequenceBaseAddress == 0x3000 &&
             layout->instrumentTableAddress == 0x2000 && layout->spcDirAddress == 0x4000 &&
             layout->initialTempo == 0x52 && layout->initialMasterVolume == 0x78 &&
             layout->initialEchoDelay == 5 && layout->initialEchoVolume == 0x1e &&
             layout->initialEchoFeedback == 0x5a && layout->initialEchoFilter == 2 && layout->tracks[0].active &&
             layout->tracks[0].playlistAddress == 0x3100,
         "NeverlandSnes should recover relocated song, instrument, and DSP directory state from driver code");

  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "NeverlandSnes fixture.aram"}, fixture);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection* collection = snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
  expect(snapshot.diagnostics().empty() && snapshot.collections().size() == 1 && collection->members.sequence &&
             collection->members.soundBanks.size() == 1 && collection->members.samplePools.empty(),
         "NeverlandSnes scanning should publish one sequence and self-contained BRR sound bank");
  const auto* bank = snapshot.asset<SoundBankAsset>(collection->members.soundBanks.front());
  expect(bank != nullptr && bank->instruments.size() == 1 && bank->instruments.front().regions.size() == 1,
         "the referenced program should retain its ADSR, tuning, and BRR sample region");
  const double expectedUnity = 120.0 - 12.0 * std::log2((0x217d / 4096.0) * 2.9375);
  expect(std::abs(bank->instruments.front().regions.front().unityKey - expectedUnity) < 0.000001,
         "NeverlandSnes instrument tuning should use the driver's unsigned 8.8 source byte order");

  std::vector<u8> original(kAramSize);
  writeBytes(original, 0x0100, {0x8f, 0x10, 0x08, 0x8f, 0x28, 0x09, 0xcd, 0x04, 0xe8, 0x00, 0xd5,
                                0x18, 0x03, 0x3d, 0xc8, 0x08, 0xd0, 0xf8, 0x8d, 0x00, 0xf7, 0x08});
  writeBytes(original, 0x0200,
             {0x1c, 0x1c, 0x60, 0x88, 0x00, 0xc4, 0x18, 0xe8, 0x00, 0x88, 0x61, 0xc4, 0x19});
  writeBytes(original, 0x0300, {0x5d, 0x60, 0x4d, 0x00, 0x0d, 0x00, 0x3c, 0x00, 0x2c, 0x00, 0xff});
  writeBytes(original, 0x2800, {'S', 'F', 'C', 0x48});
  std::fill(original.begin() + 0x2810, original.begin() + 0x2818, 0xff);
  original[0x2810] = 0;
  writeLe16(original, 0x2820, 0x2900);
  writeBytes(original, 0x2900, {0x2a, 0x00, 0xff});
  writeBytes(original, 0x2a00, {0x3c, 4, 3, 0x7f, 0xfd});
  writeBytes(original, 0x6100, {0x8f, 0xe0, 0x00, 0x10});
  writeLe16(original, 0x6000, 0x6200);
  writeLe16(original, 0x6002, 0x6200);
  writeBytes(original, 0x6200, {1, 0, 0, 0, 0, 0, 0, 0, 0});
  const auto early = findLayout(ByteReader{SourceId{303}, original});
  expect(early && early->version == Version::Original && early->sequenceBaseAddress == 0x2800 &&
             early->instrumentTableAddress == 0x6100 && early->spcDirAddress == 0x6000 &&
             early->tracks[0].playlistAddress == 0x2900,
         "the original SFC dialect should retain absolute playlist and section addresses");
}

void playlistsCallSectionsAndRespectDialectTranspose() {
  std::vector<u8> bytes(0x80);
  writeBytes(bytes, 0, {0x82, 0x00, 0x10, 0x00, 0x20, 0xff});
  writeBytes(bytes, 0x10, {0x3c, 4, 3, 0x7f, 0xfd});
  writeBytes(bytes, 0x20, {0xbd, 0xfd});
  const PerformanceSequence performance = render(bytes);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->key == 86.0 && notes[0]->header.tick == 0 &&
             notes[1]->key == 85.0 && notes[1]->header.tick == 4,
         "playlist entries should call sections, preserve note memory, and clear transpose at each FD");

  const PerformanceSequence original = render(std::move(bytes), runtimeLayout(Version::Original));
  const auto originalNotes = events<NotePerformanceEvent>(original.tracks.front());
  expect(original.diagnostics.empty() && originalNotes.size() == 2 && originalNotes[1]->key == 85.0,
         "the original driver should also clear playlist transpose at each FD");
}

void playlistTransposeDoesNotLeakIntoLaterSections() {
  std::vector<u8> bytes(0x80);
  writeBytes(bytes, 0, {0x82, 0x00, 0x10, 0x00, 0x20, 0x00, 0x30, 0x85,
                         0x00, 0x40, 0x00, 0x50, 0xff});
  for (u32 address : {0x10u, 0x20u, 0x30u, 0x40u, 0x50u}) {
    writeBytes(bytes, address, {0x3c, 1, 1, 0x7f, 0xfd});
  }

  for (Version version : {Version::Original, Version::Modern}) {
    const PerformanceSequence performance = render(bytes, runtimeLayout(version));
    const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
    expect(performance.diagnostics.empty() && notes.size() == 5 && notes[0]->key == 86.0 &&
               notes[1]->key == 84.0 && notes[2]->key == 84.0 && notes[3]->key == 89.0 &&
               notes[4]->key == 84.0,
           "playlist transpose markers should affect only the immediately following section");
  }
}

void repeatFramesCanSpanSections() {
  std::vector<u8> bytes(0x80);
  writeBytes(bytes, 0, {0x00, 0x10, 0x00, 0x20, 0xff});
  writeBytes(bytes, 0x10, {0xfb, 0x3c, 1, 1, 0x7f, 0xfd});
  writeBytes(bytes, 0x20, {0xbd, 0xfc, 2, 0xfd});
  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 4 && notes[0]->key == 84.0 && notes[1]->key == 85.0 &&
             notes[2]->key == 84.0 && notes[3]->key == 85.0 && notes.back()->header.tick == 3,
         "loop frames should restore the voice address and playlist cursor across section boundaries");
}

void reusedSectionsDoNotImplyLoops() {
  std::vector<u8> bytes(0x80);
  writeBytes(bytes, 0, {0x00, 0x10, 0x00, 0x20, 0x00, 0x20, 0x00, 0x30, 0xff});
  writeBytes(bytes, 0x10, {0xfb, 0x3c, 1, 1, 0x7f, 0xfd});
  writeBytes(bytes, 0x20, {0x3d, 1, 1, 0x7f, 0xfd});
  writeBytes(bytes, 0x30, {0x3e, 1, 1, 0x7f, 0xfc, 0});

  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && performance.tracks.front().endTick == 4 && notes.size() == 4 &&
             notes[0]->key == 84.0 && notes[1]->key == 85.0 && notes[2]->key == 85.0 && notes[3]->key == 86.0,
         "reusing a section should not stop playback before the driver's explicit loop command");
}

void excessRepeatStartsAreIgnored() {
  std::vector<u8> bytes(0x40);
  writeBytes(bytes, 0, {0x00, 0x10, 0xff});
  writeBytes(bytes, 0x10, {0xfb, 0xfb, 0xfb, 0x3c, 1, 1, 0x7f, 0xfc, 1, 0xfc, 1, 0xfd});

  const PerformanceSequence performance = render(std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && performance.tracks.front().endTick == 1 && notes.size() == 1,
         "a third nested repeat start should be ignored, matching the driver's two-slot repeat stack");
}

SourceAnnotation extendedCommandAnnotation(Version version, bool pitchDrift, u8 command) {
  std::vector<u8> bytes(0x100);
  writeBytes(bytes, 0, {0x00, 0x60, 0xff});
  writeBytes(bytes, 0x60, {0xff, command, 0, 0xfd});
  Layout layout = runtimeLayout(version);
  layout.hasPitchDrift = pitchDrift;
  const ByteReader reader{SourceId{304}, bytes};
  SourceMapBuilder sourceMap;
  static_cast<void>(decodeSequence(reader, layout, AssetId{304}, &sourceMap));
  const SourceMap annotations = sourceMap.finish();
  return commandAnnotationAt(annotations, reader.source(), 0x60);
}

void extendedCommandsRetainSourceMetadata() {
  const SourceAnnotation noise = extendedCommandAnnotation(Version::Original, false, 0x11);
  const SourceAnnotation reserved = extendedCommandAnnotation(Version::Modern, false, 0x05);
  const SourceAnnotation drift = extendedCommandAnnotation(Version::Modern, true, 0x05);
  expect(noise.label == "DSP Noise On" && noise.playbackStatus == CommandPlaybackStatus::SourceOnly &&
             noise.category() == "noise" && reserved.label == "Reserved Extended Command" &&
             reserved.sequenceSemantic == SequenceSemantic::Meta && reserved.category() == "reserved" &&
             drift.label == "Pitch Drift Up" && drift.sequenceSemantic == SequenceSemantic::Pitch &&
             drift.playbackStatus == CommandPlaybackStatus::AffectsPlayback,
         "extended commands should retain source-only, reserved, and playback metadata");
}

void modernEffectsRetainPhysicalDriverState() {
  std::vector<u8> bytes(0x100);
  writeBytes(bytes, 0, {0x00, 0x10, 0xff});
  writeBytes(bytes, 0x10,
             {0xf0, 1,    0x40, 0xff, 0x16, 0x30, 0xff, 0x18, 0x10, 0xff, 0x15, 0x20, 0xff, 0x17,
              0x08, 0xff, 0x08, 0x07, 0xff, 0x09, 0x05, 0xff, 0x0a, 0x03, 0xff, 0x0b, 0x1c, 0xff,
              0x03, 0,    0xff, 0x0d, 0x40, 0xff, 0x01, 0xc0, 0xff, 0x00, 5,    0xff, 0x1d, 0,
              0xff, 0x19, 0,    0xf4, 2,    0x50, 0x3c, 4,    3,    0x7f, 0xfd});
  const PerformanceSequence performance = render(std::move(bytes));
  const PerformanceTrack& track = performance.tracks.front();
  const auto vibrato = modulationEvents(track, ModulationPerformanceTarget::VibratoDepth);
  const auto vibratoRate = modulationEvents(track, ModulationPerformanceTarget::VibratoRate);
  const auto tremolo = modulationEvents(track, ModulationPerformanceTarget::TremoloDepth);
  const auto tremoloRate = modulationEvents(track, ModulationPerformanceTarget::TremoloRate);
  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto reverbs = events<ReverbPerformanceEvent>(track);
  const auto balances = events<StereoBalancePerformanceEvent>(track);
  const auto tempos = events<TempoPerformanceEvent>(track);

  expect(performance.diagnostics.empty() && vibrato.size() >= 2 && vibrato.back()->context.shape &&
             vibrato.back()->context.shape->samples.size() == 256 && vibrato.back()->context.frequencyHz == 4.0 &&
             vibratoRate.back()->context.frequencyHz == 2.0 && tremolo.size() >= 2 &&
             tremolo.back()->context.frequencyHz == 4.0 && tremoloRate.back()->context.frequencyHz == 1.0,
         "modern modulation should retain the 64 Hz quantized sine, independent rates, depths, and shared strength");
  expect(envelopes.size() == 4 && envelopes.front()->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelopes[0]->update.fields == EnvelopeFields::Attack &&
             envelopes[1]->update.fields == EnvelopeFields::Decay &&
             envelopes[2]->update.fields == EnvelopeFields::Sustain &&
             envelopes[3]->update.fields == EnvelopeFields::SecondDecay,
         "dynamic ADSR commands should update current DSP voices and all later attacks");
  expect(!reverbs.empty() && reverbs.back()->voiceMask == 1 && reverbs.back()->delayMilliseconds == 80.0 &&
             reverbs.back()->feedback == -0.5 && reverbs.back()->leftGain == 56.0 / 128.0 &&
             reverbs.back()->rightGain == -56.0 / 128.0,
         "echo should retain EON, EDL, signed feedback, asymmetric volume, and output phase inversion");
  expect(!balances.empty() && balances.back()->leftGain == -0.5 && balances.back()->rightGain == 0.5 &&
             !tempos.empty() && tempos.back()->microsecondsPerQuarter == 0x50 * 6000u,
         "dry phase inversion and timer-0 tempo should preserve the driver's physical values");
}

void originalAdsrSiblingResetAndEnergyDriftAreAudited() {
  std::vector<u8> original(0x80);
  writeBytes(original, 0, {0x00, 0x10, 0xff});
  writeBytes(original, 0x10, {0xff, 0x0b, 0x07, 0xff, 0x0c, 0x05, 0xff, 0x0d, 3,
                              0xff, 0x0e, 0x1c, 0x3c, 4, 3, 0x7f, 0xfd});
  const PerformanceSequence early = render(std::move(original), runtimeLayout(Version::Original));
  const auto earlyEnvelopes = events<EnvelopePerformanceEvent>(early.tracks.front());
  expect(early.diagnostics.empty() && earlyEnvelopes.size() == 8 && !earlyEnvelopes[0]->update.values &&
             earlyEnvelopes[0]->update.fields == EnvelopeFields::Decay &&
             earlyEnvelopes[2]->update.fields == EnvelopeFields::Attack &&
             earlyEnvelopes[4]->update.fields == EnvelopeFields::SecondDecay &&
             earlyEnvelopes[6]->update.fields == EnvelopeFields::Sustain,
         "original ADSR field commands should restore the sibling bits reloaded from the instrument table");

  Layout energy = runtimeLayout();
  energy.hasPitchDrift = true;
  std::vector<u8> drift(0x80);
  writeBytes(drift, 0, {0x00, 0x10, 0xff});
  writeBytes(drift, 0x10, {0xff, 0x05, 4, 0x3c, 12, 12, 0x7f, 0xff, 0x07, 0, 0xfd});
  const PerformanceSequence moved = render(std::move(drift), energy);
  const auto bends = events<PitchBendPerformanceEvent>(moved.tracks.front());
  expect(moved.diagnostics.empty() && std::ranges::any_of(bends, [](const PitchBendPerformanceEvent* event) {
           return event->semitones > 0.0;
         }),
         "Energy Breaker's FF 05 drift should accumulate on the fixed 64 Hz driver clock");
}

}  // namespace

void runNeverlandSnesModuleTests() {
  relocatedLayoutUsesDriverCodeAndHeaderContracts();
  playlistsCallSectionsAndRespectDialectTranspose();
  playlistTransposeDoesNotLeakIntoLaterSections();
  repeatFramesCanSpanSections();
  reusedSectionsDoNotImplyLoops();
  excessRepeatStartsAreIgnored();
  extendedCommandsRetainSourceMetadata();
  modernEffectsRetainPhysicalDriverState();
  originalAdsrSiblingResetAndEnergyDriftAreAudited();
}

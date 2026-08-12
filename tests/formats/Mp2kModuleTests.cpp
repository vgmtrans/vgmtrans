/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/PsfExtractor.h"
#include "value/formats/MP2k/MP2k.h"
#include "value/formats/MP2k/MP2kEnvelope.h"
#include "value/session/Session.h"
#include "value/synth/SampleDecoder.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::mp2k;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void le16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void le32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
  bytes[offset + 2] = static_cast<u8>(value >> 16);
  bytes[offset + 3] = static_cast<u8>(value >> 24);
}

[[nodiscard]] std::vector<u8> mp2kFixture(u8 directSoundMasterVolume = 15) {
  std::vector<u8> bytes(0x1000);
  constexpr std::array<u8, 30> signature{
      0x00, 0xb5, 0x00, 0x04, 0x07, 0x4a, 0x08, 0x49, 0x40, 0x0b, 0x40, 0x18, 0x83, 0x88, 0x59,
      0x00, 0xc9, 0x18, 0x89, 0x00, 0x89, 0x18, 0x0a, 0x68, 0x01, 0x68, 0x10, 0x1c, 0x00, 0xf0,
  };
  le32(bytes, 0x100, 0x00980800 | (static_cast<u32>(directSoundMasterVolume) << 12));
  le32(bytes, 0x104, 0);
  le32(bytes, 0x108, 0x08000200);
  le16(bytes, 0x110, 0xb500);
  std::copy(signature.begin(), signature.end(), bytes.begin() + 0x118);

  le32(bytes, 0x200, 0x08000300);
  le32(bytes, 0x208, 0xffffffff);
  bytes[0x300] = 1;
  bytes[0x302] = 10;
  bytes[0x303] = 40;
  le32(bytes, 0x304, 0x08000400);
  le32(bytes, 0x308, 0x08000320);

  const std::vector<u8> track{
      0xbb, 75,                           // tempo
      0xbd, 0,                            // program
      0xbe, 100,                          // volume
      0xb4,                               // top-level pattern end (ignored by the driver)
      0xbf, 64,                           // pan
      0xc2, 22,                           // LFO speed
      0xc3, 3,                            // LFO delay
      0xc5, 0,                            // vibrato
      0xc4, 8,                            // 50-cent depth
      0xcd, 8,   24,                      // pseudo-echo volume (not reverb)
      0xcd, 9,   12,                      // pseudo-echo length
      0xcd, 10,  32,                      // CGB length
      0xcd, 11,  0x08,                    // CGB sweep
      0xcd, 13,  0x78, 0x56, 0x34, 0x12,  // sample start (later MP2k revision)
      0xd4, 60,  100,                     // five-tick note
      0x81,                               // wait one
      62,   110,                          // running-status note
      0xcd, 4,   0x80,                    // dynamic attack
      0xcd, 5,   0xf0,                    // dynamic decay
      0xcd, 6,   0x60,                    // dynamic sustain
      0xcd, 7,   0xe0,                    // dynamic release
      0xc5, 1,                            // tremolo
      0xc4, 16,                           // 12.5% linear-gain excursion
      0xc5, 2,                            // pan LFO
      0xc4, 16,  0xc5, 0,                 // vibrato again
      0xc4, 8,   0xc3, 0,                 // a zero delay preserves the running phase on notes
      0xcf, 64,  100,                     // tied note
      0x82,                               // wait two
      0xce, 64,                           // end tied key
      0xb1,
  };
  std::copy(track.begin(), track.end(), bytes.begin() + 0x320);

  // DirectSound, programmable-wave, and square instruments.
  bytes[0x400] = 0;
  bytes[0x401] = 60;
  le32(bytes, 0x404, 0x08000b00);
  bytes[0x408] = 0xff;
  bytes[0x409] = 0xf0;
  bytes[0x40a] = 0xc0;
  bytes[0x40b] = 0xe0;
  bytes[0x40c] = 3;
  bytes[0x40d] = 60;
  le32(bytes, 0x410, 0x08000c00);
  bytes[0x414] = 1;
  bytes[0x415] = 1;
  bytes[0x418] = 2;
  bytes[0x419] = 60;
  le32(bytes, 0x41c, 1);
  bytes[0x420] = 3;
  bytes[0x421] = 3;
  bytes[0x422] = 15;
  bytes[0x423] = 3;

  le16(bytes, 0xb00, 0);
  bytes[0xb03] = 0x40;
  le32(bytes, 0xb04, 26758 * 1024);
  le32(bytes, 0xb08, 8);
  le32(bytes, 0xb0c, 16);
  for (u32 i = 0; i < 16; ++i) {
    bytes[0xb10 + i] = static_cast<u8>(static_cast<s8>(i * 8 - 64));
  }
  for (u32 i = 0; i < 16; ++i) {
    bytes[0xc00 + i] = static_cast<u8>((i << 4) | (15 - i));
  }
  return bytes;
}

[[nodiscard]] std::vector<u8> psf22(std::span<const u8> executable, std::string_view tags = {}) {
  uLongf compressedSize = compressBound(executable.size());
  std::vector<u8> compressed(compressedSize);
  expect(compress2(compressed.data(), &compressedSize, executable.data(), executable.size(), Z_BEST_SPEED) == Z_OK,
         "GSF fixture should compress");
  compressed.resize(compressedSize);

  std::vector<u8> result(16 + compressed.size());
  result[0] = 'P';
  result[1] = 'S';
  result[2] = 'F';
  result[3] = 0x22;
  le32(result, 8, static_cast<u32>(compressed.size()));
  le32(result, 12, static_cast<u32>(crc32(crc32(0, nullptr, 0), compressed.data(), compressed.size())));
  std::copy(compressed.begin(), compressed.end(), result.begin() + 16);
  result.insert(result.end(), tags.begin(), tags.end());
  return result;
}

[[nodiscard]] std::vector<u8> gsf(std::span<const u8> rom, u32 omittedZeroBytes = 0) {
  std::vector<u8> executable(12 + rom.size());
  le32(executable, 0, 0x08000000);
  le32(executable, 4, 0x08000000);
  le32(executable, 8, static_cast<u32>(rom.size()) + omittedZeroBytes);
  std::copy(rom.begin(), rom.end(), executable.begin() + 12);
  return psf22(executable);
}

[[nodiscard]] std::vector<u8> miniGsf(u8 song, std::string_view library) {
  std::vector<u8> executable(13);
  le32(executable, 0, 0x08000000);
  le32(executable, 4, 0x08000020);
  le32(executable, 8, 1);
  executable[12] = song;
  return psf22(executable, "[TAG]\n_lib=" + std::string(library) + "\ntitle=Selected Song\n");
}

void mp2kModuleBuildsAuditedSequenceAndSynth() {
  constexpr double gbaFrameRate = 16777216.0 / 280896.0;
  const std::vector<u8> bytes = mp2kFixture();
  Session session;
  session.registerFormat(mp2kDefinition());
  const SourceId source = session.addSource(SourceFile{.name = "mp2k-fixture.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "MP2k fixture should produce one collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.instrumentSets.size() == 1 && collection.members.sampleCollections.size() == 2,
         "MP2k collection should attach its bank, PCM samples, and shared PSG samples");

  const auto* instruments = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(instruments != nullptr && instruments->instruments.size() == 3,
         "MP2k bank should retain DirectSound, wave-RAM, and square programs");
  expect(instruments->instruments[0].modulation.vibrato && instruments->instruments[0].modulation.tremolo &&
             instruments->instruments[0].modulation.vibrato->waveform == LfoWaveform::Triangle,
         "MP2k instruments should advertise the audited triangle vibrato and tremolo ranges");
  expect(std::abs(instruments->instruments[0].regions.front().attenuationDb) < 1e-12 &&
             std::abs(instruments->instruments[1].regions.front().attenuationDb - 20.0 * std::log10(2.0)) < 1e-12 &&
             std::abs(instruments->instruments[2].regions.front().attenuationDb - 20.0 * std::log10(2.0)) < 1e-12,
         "full-scale DirectSound must remain at unity while CGB voices retain the driver's single-lane mixer "
         "scaling");
  expect(instruments->instruments[0].regions.front().envelope.attackSeconds == 0.0,
         "DirectSound attack 0xff must be at full scale on the first mixer frame");
  expect(instruments->instruments[0].regions.front().envelope.holdSeconds &&
             std::abs(*instruments->instruments[0].regions.front().envelope.holdSeconds - 1.0 / gbaFrameRate) < 1e-12,
         "DirectSound must hold its peak until the mixer frame after attack completes");
  expect(instruments->instruments[0].regions.front().envelope.releaseSeconds &&
             std::abs(*instruments->instruments[0].regions.front().envelope.releaseSeconds -
                      5.0 * std::log(10.0) / (gbaFrameRate * std::log(256.0 / 224.0))) < 1e-12,
         "DirectSound release must preserve the driver's dB-per-frame slope across SoundFont's 100 dB range");
  expect(directReleaseSeconds(0) == 0.0, "DirectSound release zero must silence the first release mixer pass");
  expect(directDecaySeconds(0) == 0.0,
         "DirectSound decay zero must silence the first decay pass after the separate peak hold frame");
  expect(std::abs(directAttackSeconds(127) - 258.0 / (255.0 * gbaFrameRate)) < 1e-12,
         "DirectSound attack conversion must retain the final partial integer step");

  const auto* firstSamples = snapshot.asset<SampleCollectionAsset>(collection.members.sampleCollections[0]);
  const auto* secondSamples = snapshot.asset<SampleCollectionAsset>(collection.members.sampleCollections[1]);
  const auto* psg = firstSamples && firstSamples->samples.samples.size() > 1 ? firstSamples : secondSamples;
  const auto* pcm = psg == firstSamples ? secondSamples : firstSamples;
  expect(psg != nullptr && psg->samples.samples.size() == 7 && pcm != nullptr && pcm->samples.samples.size() == 1,
         "MP2k synth should generate square and both noise-width PSG sounds plus the referenced wave-RAM sound");
  expect(instruments->instruments[1].regions.size() == 128 && instruments->instruments[2].regions.size() == 128,
         "melodic PSG regions should retain the driver's key-clamped hardware frequency registers");
  const auto& waveA4 = instruments->instruments[1].regions[69];
  const auto& squareA4 = instruments->instruments[2].regions[69];
  const double waveA4Hertz = 440.0 * std::exp2((69.0 - waveA4.unityKey) / 12.0);
  const double squareA4Hertz = 440.0 * std::exp2((69.0 - squareA4.unityKey) / 12.0);
  expect(std::abs(waveA4Hertz - 65536.0 / 298.0) < 1e-9 && std::abs(squareA4Hertz - 131072.0 / 298.0) < 1e-9,
         "programmable wave must use half the square clock after the exact MP2k frequency-table lookup");
  const auto decodedPcm = decodeSample(pcm->samples.samples.front(), session.sources().bytes(source));
  expect(decodedPcm && decodedPcm->pcm.size() == 16 && decodedPcm->loop.enabled && decodedPcm->loop.start == 8,
         "MP2k DirectSound samples should preserve PCM data and loop points");
  const auto decodedWave = decodeSample(psg->samples.samples.back(), session.sources().bytes(source));
  expect(decodedWave && decodedWave->pcm.size() == 32 && decodedWave->pcm[0] == -32768 && decodedWave->pcm[16] == 0 &&
             decodedWave->pcm[30] == 28672,
         "programmable-wave samples should retain the GBA DAC's exact asymmetric -16..14 range");

  const CollectionPlayback playback =
      session.preparePlayback(collection.id, PlaybackRequest{.sequence = {.sequenceLoops = 0}});
  expect(playback.playable() && playback.performance.tracks.size() == 1,
         "MP2k collection should prepare source-free playback");
  const auto& events = playback.performance.tracks.front().events;
  expect(std::ranges::any_of(events,
                             [](const PerformanceEvent& event) {
                               const auto* value = std::get_if<LevelPerformanceEvent>(&event);
                               return value && value->linearGain == 0.0;
                             }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* value = std::get_if<ExpressionPerformanceEvent>(&event);
                                   return value && value->linearGain == 1.0;
                                 }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* value = std::get_if<StereoBalancePerformanceEvent>(&event);
                                   return value && std::abs(value->leftGain - 127.0 / 256.0) < 1e-12 &&
                                          std::abs(value->rightGain - 128.0 / 256.0) < 1e-12;
                                 }),
         "MP2k playback should initialize cleared VOL, unit volX/expression, and hardware center balance separately");
  const auto level = std::ranges::find_if(events, [](const PerformanceEvent& event) {
    const auto* value = std::get_if<LevelPerformanceEvent>(&event);
    return value && value->linearGain != 0.0;
  });
  expect(level != events.end() &&
             std::abs(std::get<LevelPerformanceEvent>(*level).linearGain - 100.0 / 127.0) < 1e-12 &&
             std::get<LevelPerformanceEvent>(*level).sourceQuantization &&
             std::get<LevelPerformanceEvent>(*level).sourceQuantization->levels == 128,
         "MP2k VOL must be a linear 7-bit hardware gain, not a squared MIDI controller curve");
  const auto pan = std::ranges::find_if(
      events, [](const PerformanceEvent& event) { return std::holds_alternative<PanPerformanceEvent>(event); });
  expect(pan != events.end() && std::get<PanPerformanceEvent>(*pan).law == PanLaw::ConstantSum &&
             std::abs(std::get<PanPerformanceEvent>(*pan).stereoPosition - 1.0 / 255.0) < 1e-12 &&
             std::get<PanPerformanceEvent>(*pan).hasLinearGain &&
             std::abs(std::get<PanPerformanceEvent>(*pan).linearGain - 255.0 / 256.0) < 1e-12,
         "MP2k pan should retain TrkVolPitSet's asymmetric constant-sum channel factors");
  expect(std::ranges::count_if(
             events,
             [](const PerformanceEvent& event) { return std::holds_alternative<NotePerformanceEvent>(event); }) == 3,
         "top-level PEND must fall through to explicit, running-status, and tied MP2k notes");
  const auto firstNote = std::ranges::find_if(
      events, [](const PerformanceEvent& event) { return std::holds_alternative<NotePerformanceEvent>(event); });
  expect(firstNote != events.end() &&
             std::abs(std::get<NotePerformanceEvent>(*firstNote).linearVelocity - 100.0 / 127.0) < 1e-12,
         "MP2k note velocity must remain a separate linear channel-gain lane");
  std::vector<const NotePerformanceEvent*> noteEvents;
  for (const auto& event : events) {
    if (const auto* noteEvent = std::get_if<NotePerformanceEvent>(&event)) {
      noteEvents.push_back(noteEvent);
    }
  }
  expect(noteEvents.size() == 3 && noteEvents[0]->restartsLfoPhase && noteEvents[1]->restartsLfoPhase &&
             !noteEvents[2]->restartsLfoPhase,
         "notes should reload and reset the MP2k LFO only while LFODL is nonzero");
  expect(std::ranges::count_if(events,
                               [](const PerformanceEvent& event) {
                                 return std::holds_alternative<EnvelopePerformanceEvent>(event);
                               }) == 4,
         "XCMD attack, decay, sustain, and release should emit dynamic envelope updates");
  const auto attack = std::ranges::find_if(events, [](const PerformanceEvent& event) {
    const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event);
    return envelope && hasEnvelopeField(envelope->update.fields, EnvelopeFields::Attack);
  });
  expect(attack != events.end() && std::get<EnvelopePerformanceEvent>(*attack).update.values &&
             std::get<EnvelopePerformanceEvent>(*attack).update.values->attackSeconds &&
             std::abs(*std::get<EnvelopePerformanceEvent>(*attack).update.values->attackSeconds -
                      254.0 / (255.0 * gbaFrameRate)) < 1e-12,
         "DirectSound attack should preserve the exact area of the pre-advanced integer staircase");
  expect(std::ranges::count_if(
             events,
             [](const PerformanceEvent& event) { return std::holds_alternative<ReverbPerformanceEvent>(event); }) == 1,
         "XCMD pseudo echo must not create a second reverb lane event");
  expect(std::ranges::any_of(events,
                             [](const PerformanceEvent& event) {
                               const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                               return modulation && modulation->waveform == LfoWaveform::Triangle &&
                                      modulation->cyclesPerTick &&
                                      std::abs(*modulation->cyclesPerTick - 22.0 / 256.0) < 1e-12 &&
                                      modulation->delayTicks == 3 && modulation->delayAppliesOnNoteRestartOnly &&
                                      !modulation->delayRunsWhileInactive &&
                                      modulation->initialPhaseCycles == 22.0 / 256.0;
                             }),
         "MP2k LFO commands should retain the audited pre-incremented phase and note-loaded delay");
  expect(std::ranges::any_of(events,
                             [](const PerformanceEvent& event) {
                               const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                               return modulation && modulation->target == ModulationPerformanceTarget::TremoloDepth &&
                                      modulation->volumeDepthLinearGain == 16.0 / 128.0 &&
                                      modulation->tremoloGainMode == TremoloGainMode::BipolarAroundNominal;
                             }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                                   return modulation && modulation->target == ModulationPerformanceTarget::PanDepth &&
                                          modulation->panDepth == 32.0 / 255.0;
                                 }),
         "MP2k MODT should expose its exact linear tremolo and pan excursions");
}

void mp2kBdpcmDecoderUsesDecodedSampleCount() {
  std::vector<u8> source(33);
  source[0] = 10;
  source[1] = 1;
  source[2] = 0x12;
  const Sample sample{
      .codec = AudioCodec::GbaBdpcm,
      .encodedData = SourceRange{.source = SourceId{1}, .offset = 0, .size = source.size()},
      .sampleRate = 26758,
      .codecParameter = 4,
  };
  const auto decoded = decodeSample(sample, source);
  expect(decoded && decoded->pcm.size() == 4 && decoded->pcm[0] == 10 * 256 && decoded->pcm[1] == 11 * 256 &&
             decoded->pcm[2] == 12 * 256 && decoded->pcm[3] == 16 * 256,
         "GBA BDPCM should decode the low-only first delta byte and stop at WaveData::size");
}

void mp2kReverseBdpcmDecodesInPlaybackOrder() {
  std::vector<u8> source(33);
  source[0] = 10;
  source[1] = 1;
  source[2] = 0x12;
  const Sample sample{
      .codec = AudioCodec::GbaBdpcm,
      .encodedData = SourceRange{.source = SourceId{1}, .offset = 0, .size = source.size()},
      .sampleRate = 26758,
      .reverse = true,
      .codecParameter = 4,
  };
  const auto decoded = decodeSample(sample, source);
  expect(decoded && decoded->pcm == std::vector<s16>({16 * 256, 12 * 256, 11 * 256, 10 * 256}),
         "reverse BDPCM should decode blocks forward before reversing decoded playback samples");
}

void mp2kFixedReverseDirectSoundUsesMixerRate() {
  std::vector<u8> bytes = mp2kFixture();
  bytes[0x400] = 0x18;  // DirectSound FIX | REV
  Session session;
  session.registerFormat(mp2kDefinition());
  const SourceId source = session.addSource(SourceFile{.name = "mp2k-fixed-reverse.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection& collection = snapshot.collections().front();
  const auto* instruments = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(instruments && instruments->instruments[0].regions.size() == 128 &&
             instruments->instruments[0].regions[60].keyRange.low == 60 &&
             instruments->instruments[0].regions[60].unityKey == 60.0,
         "DirectSound FIX should preserve the mixer-rate pitch independently for every played key");
  const auto* firstSamples = snapshot.asset<SampleCollectionAsset>(collection.members.sampleCollections[0]);
  const auto* secondSamples = snapshot.asset<SampleCollectionAsset>(collection.members.sampleCollections[1]);
  const auto* pcm = firstSamples && firstSamples->samples.samples.size() == 1 ? firstSamples : secondSamples;
  expect(pcm && pcm->samples.samples.front().reverse && !pcm->samples.samples.front().loop.enabled,
         "reverse DirectSound should walk the sample backward and bypass the forward-only loop branch");
  const auto decoded = decodeSample(pcm->samples.samples.front(), session.sources().bytes(source));
  expect(decoded && decoded->pcm.front() == 56 * 256 && decoded->pcm.back() == -64 * 256,
         "reverse PCM should expose the same order SoundMainRAM mixes");
}

void mp2kNoiseUsesAuditedRegisterClockAndWidth() {
  std::vector<u8> bytes = mp2kFixture();
  bytes[0x418] = 4;       // CGB noise
  le32(bytes, 0x41c, 1);  // 7-bit/short LFSR
  Session session;
  session.registerFormat(mp2kDefinition());
  const SourceId source = session.addSource(SourceFile{.name = "mp2k-noise.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection& collection = snapshot.collections().front();
  const auto* instruments = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(instruments && instruments->instruments.size() == 3 && instruments->instruments[2].regions.size() == 128,
         "MP2k noise should preserve the key-clamped register table with one region per key");
  const Region& noiseA4 = instruments->instruments[2].regions[69];
  const double renderedClock = 26758.0 * std::exp2((69.0 - noiseA4.unityKey) / 12.0);
  const double hardwareClock = 524288.0 / 7.0 / 4.0;  // gNoiseTable[48] = 0x17
  expect(std::abs(renderedClock - hardwareClock) < 1e-9 && noiseA4.sample.index == 5,
         "noise key 69 should use register 0x17 and the tone's short-LFSR selector");
  const auto* firstSamples = snapshot.asset<SampleCollectionAsset>(collection.members.sampleCollections[0]);
  const auto* secondSamples = snapshot.asset<SampleCollectionAsset>(collection.members.sampleCollections[1]);
  const auto* psg = firstSamples && firstSamples->samples.samples.size() >= 6 ? firstSamples : secondSamples;
  expect(psg && psg->samples.samples[5].codecParameter == 5,
         "short MP2k noise should reference the 7-bit GBA LFSR sample");
  const auto decoded = decodeSample(psg->samples.samples[5], session.sources().bytes(source));
  expect(decoded && decoded->pcm.size() == 127 && decoded->loop.enabled && decoded->loop.length == 127,
         "the short noise sample should contain one exact 7-bit LFSR period");
}

void mp2kCgbFixedToneUsesDacResolutionMask() {
  std::vector<u8> bytes = mp2kFixture();
  bytes[0x418] = 0x0a;  // channel 2 | FIX
  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-cgb-fixed.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection& collection = snapshot.collections().front();
  const auto* instruments = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(instruments && instruments->instruments.size() == 3 && instruments->instruments[2].regions.size() == 128,
         "CGB FIX fixture should retain singleton hardware-pitch regions");
  const Region& key37 = instruments->instruments[2].regions[37];
  const double renderedFrequency = 440.0 * std::exp2((37.0 - key37.unityKey) / 12.0);
  expect(std::abs(renderedFrequency - 131072.0 / (2048.0 - 158.0)) < 1e-9,
         "8-bit DAC mode should round a CGB FIX register upward to an even value");
}

void mp2kCgbLengthClampsSequenceGateInPhysicalTime() {
  std::vector<u8> bytes = mp2kFixture();
  const std::array<u8, 15> track{0xbb, 75, 0xbd, 2, 0xcd, 10, 32, 0xff, 60, 100, 0xb0, 0xb1, 0, 0, 0};
  std::copy(track.begin(), track.end(), bytes.begin() + 0x320);
  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-cgb-length.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const CollectionPlayback playback = session.preparePlayback(snapshot.collections().front().id, PlaybackRequest{});
  const auto note = std::ranges::find_if(playback.performance.tracks.front().events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(note != playback.performance.tracks.front().events.end() &&
             std::get<NotePerformanceEvent>(*note).maximumDurationMilliseconds == 125.0,
         "CGB length 32 should stop a square voice after (64-32)/256 seconds");
}

void mp2kCgbVolumeUsesCombinedHardwareQuantization() {
  std::vector<u8> bytes = mp2kFixture();
  bytes[0x416] = 9;
  bytes[0x417] = 1;
  const std::array<u8, 19> track{
      0xbd, 2, 0xbe, 82, 0xd4, 69, 32,  0x81,  // square: reference goal 10, note goal 2
      0xbd, 1, 0xbe, 58, 0xd4, 50, 119, 0x81,  // wave: reference goal 7, note goal 6
      0xb1, 0, 0,
  };
  std::copy(track.begin(), track.end(), bytes.begin() + 0x320);
  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-cgb-volume.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const CollectionPlayback playback = session.preparePlayback(snapshot.collections().front().id, PlaybackRequest{});
  const auto& events = playback.performance.tracks.front().events;

  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 2 && std::abs(notes[0]->linearVelocity - 0.2) < 1e-12 &&
             std::abs(notes[1]->linearVelocity - 1.0) < 1e-12,
         "CGB note velocity should be the ratio of quantized note and reference hardware levels");
  expect(std::ranges::any_of(events,
                             [](const PerformanceEvent& event) {
                               const auto* level = std::get_if<LevelPerformanceEvent>(&event);
                               return level && level->header.tick == 0 &&
                                      std::abs(level->linearGain - 10.0 / 15.0) < 1e-12;
                             }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* level = std::get_if<LevelPerformanceEvent>(&event);
                                   return level && level->header.tick == 1 && std::abs(level->linearGain - 0.5) < 1e-12;
                                 }),
         "CGB track level should use the 4-bit square goal and five-level wave-volume register map");
  expect(std::ranges::any_of(events,
                             [](const PerformanceEvent& event) {
                               const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event);
                               if (!envelope || envelope->header.tick != 1 || !envelope->update.values) {
                                 return false;
                               }
                               const Envelope& value = *envelope->update.values;
                               return envelope->update.fields == EnvelopeFields::All && value.attackSeconds &&
                                      value.decaySeconds && value.releaseSeconds && value.sustainAmplitude &&
                                      std::abs(*value.attackSeconds - 6.0 / 64.0) < 1e-12 &&
                                      std::abs(*value.decaySeconds - 6.0 / 64.0) < 1e-12 &&
                                      std::abs(*value.releaseSeconds - 6.0 / 64.0) < 1e-12 &&
                                      std::abs(*value.sustainAmplitude - 0.5) < 1e-12;
                             }),
         "CGB ADSR should use the note's envelope goal and quantized wave sustain ratio");
}

void mp2kUndefinedJumpSlotsUseFine() {
  std::vector<u8> bytes = mp2kFixture();
  const std::array<u8, 12> track{0xbd, 0, 0xbe, 127, 0xd4, 60, 127, 0xb6, 0xd4, 64, 127, 0xb1};
  std::copy(track.begin(), track.end(), bytes.begin() + 0x320);
  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-undefined-fine.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const CollectionPlayback playback = session.preparePlayback(snapshot.collections().front().id, PlaybackRequest{});
  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : playback.performance.tracks.front().events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 1 && notes.front()->durationTicks == 0,
         "undefined MP2k jump-table slots should invoke ply_fine and stop active channels immediately");
}

void mp2kPortConsumesItsRegisterOperands() {
  std::vector<u8> bytes = mp2kFixture();
  const std::array<u8, 12> track{0xbd, 0, 0xbe, 127, 0xcc, 0x20, 0x77, 0xd4, 60, 127, 0x81, 0xb1};
  std::copy(track.begin(), track.end(), bytes.begin() + 0x320);
  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-port.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const CollectionPlayback playback = session.preparePlayback(snapshot.collections().front().id, PlaybackRequest{});
  expect(std::ranges::count_if(
             playback.performance.tracks.front().events,
             [](const PerformanceEvent& event) { return std::holds_alternative<NotePerformanceEvent>(event); }) == 1,
         "PORT should consume its address and value bytes without terminating the following sequence");
}

void mp2kUnknownMemaccDoesNotConsumeAJumpPointer() {
  std::vector<u8> bytes = mp2kFixture();
  const std::array<u8, 14> track{0xbd, 0, 0xbe, 127, 0xb9, 18, 0, 0, 0xd4, 60, 127, 0x81, 0xb1, 0};
  std::copy(track.begin(), track.end(), bytes.begin() + 0x320);
  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-memacc.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const CollectionPlayback playback = session.preparePlayback(snapshot.collections().front().id, PlaybackRequest{});
  expect(std::ranges::count_if(
             playback.performance.tracks.front().events,
             [](const PerformanceEvent& event) { return std::holds_alternative<NotePerformanceEvent>(event); }) == 1,
         "unknown MEMACC operations should consume only their three arguments, exactly like the driver");
}

void mp2kCompatibleDriverFallbackFindsDataTable() {
  std::vector<u8> bytes = mp2kFixture();
  std::fill(bytes.begin() + 0x118, bytes.begin() + 0x118 + 30, 0);
  le32(bytes, 0x180, 0x08000200);  // replacement-driver code literal
  for (u32 song = 0; song < 4; ++song) {
    le32(bytes, 0x200 + song * 8, 0x08000300);
    le32(bytes, 0x204 + song * 8, 0);
  }
  ScanIdAllocator ids;
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "mp2k-compatible.gba"}, bytes);
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};
  ScanResultBuilder result(input, "MP2k");
  const auto layouts = findMp2kLayouts(result);
  expect(layouts.size() == 1 && layouts.front().engine.songTableOffset == 0x200 && layouts.front().songs.size() == 4,
         "a replacement driver should still be found from its referenced MP2k-compatible song table");
}

void mp2kSongSelectLiteralsFindSparseTableAndRespectPlayerCapacity() {
  std::vector<u8> bytes = mp2kFixture();
  std::fill(bytes.begin() + 0x100, bytes.begin() + 0x118, 0);  // no adjacent engine-settings block
  le32(bytes, 0x118 + 36, 0x08000d00);                         // player table literal
  le32(bytes, 0x118 + 40, 0x08000d0c);                         // song table literal
  bytes[0xd08] = 1;                                            // player 0 owns one track
  le32(bytes, 0xd0c + 42 * 8, 0x08000300);                     // sparse song ID 42
  bytes[0x300] = 2;                                            // only the first pointer is used
  le32(bytes, 0x30c, 0);

  ScanIdAllocator ids;
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "Synthetic sparse MP2k ROM"}, bytes);
  ScanInput input{.source = sources.source(source), .reader = sources.reader(source), .ids = ids};
  ScanResultBuilder result(input, "MP2k");
  const auto layouts = findMp2kLayouts(result);
  expect(layouts.size() == 1 && layouts.front().engine.songTableOffset == 0xd0c && layouts.front().songs.size() == 1 &&
             layouts.front().songs.front().index == 42 && layouts.front().songs.front().trackCount == 1,
         "SongNumStart literals should expose sparse IDs and MPlayStart should cap tracks to the selected player");
}

void mp2kDirectSoundMasterVolumeAffectsOnlyPcmVoices() {
  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-master-volume.gba"}, mp2kFixture(7));
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const auto* instruments =
      snapshot.asset<InstrumentSetAsset>(snapshot.collections().front().members.instrumentSets.front());
  expect(instruments && instruments->instruments.size() == 3 && !instruments->instruments[0].regions.empty() &&
             !instruments->instruments[1].regions.empty(),
         "MP2k master-volume fixture should retain its PCM and CGB instruments");
  expect(std::abs(instruments->instruments[0].regions.front().attenuationDb - 20.0 * std::log10(2.0)) < 1e-12 &&
             std::abs(instruments->instruments[1].regions.front().attenuationDb - 20.0 * std::log10(2.0)) < 1e-12,
         "the DirectSound master nibble and the CGB driver's independent mixer path should remain distinct");
}

void gsfExtractorFeedsMp2kValueScanner() {
  Session session;
  session.registerFormat(vgmtrans::formats::psf::psfExtractorDefinition());
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-fixture.gsf"}, gsf(mp2kFixture()));
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.sources().size() == 2 && snapshot.sources()[1].attribute("container-format") == "GSF" &&
             snapshot.collections().size() == 1,
         "PSF version 0x22 should produce a derived GBA image that the MP2k module scans");
}

void gsfExtractorZeroFillsSparseTail() {
  const std::vector<u8> rom = mp2kFixture();
  constexpr u32 omittedZeroBytes = 12;
  Session session;
  session.registerFormat(vgmtrans::formats::psf::psfExtractorDefinition());
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "Synthetic sparse-tail GSF"}, gsf(rom, omittedZeroBytes));
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.sources().size() == 2 && snapshot.sources()[1].size == rom.size() + omittedZeroBytes &&
             snapshot.collections().size() == 1 && snapshot.diagnostics().empty(),
         "GSF should zero-fill an omitted tail up to its declared image size");
}

void miniGsfOverlaysLibraryAndNamesSelectedSong() {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path() / ("vgmtrans-mp2k-" + std::to_string(unique));
  std::filesystem::create_directories(directory);
  const auto libraryPath = directory / "fixture.gsflib";
  const std::vector<u8> library = gsf(mp2kFixture());
  {
    std::ofstream stream(libraryPath, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(library.data()), static_cast<std::streamsize>(library.size()));
  }

  Session session;
  session.registerFormat(vgmtrans::formats::psf::psfExtractorDefinition());
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "selected.minigsf", .path = directory / "selected.minigsf"},
                    miniGsf(0, libraryPath.filename().string()));
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  std::filesystem::remove_all(directory);
  expect(snapshot.collections().size() == 1 && snapshot.collections().front().name == "Selected Song" &&
             snapshot.sources().size() == 2 && snapshot.sources()[1].attribute("mp2k.song-index") == "0",
         "miniGSF should overlay its gsflib and apply title metadata only to the selected MP2k song");
}

void mp2kSkipsEmptyPcmCollections() {
  std::vector<u8> bytes = mp2kFixture();
  le32(bytes, 0x208, 0x08000380);
  le32(bytes, 0x210, 0xffffffff);
  bytes[0x380] = 1;
  le32(bytes, 0x384, 0x08000500);
  le32(bytes, 0x388, 0x080003a0);
  bytes[0x3a0] = 0xb1;

  Session session;
  session.registerFormat(mp2kDefinition());
  session.addSource(SourceFile{.name = "mp2k-empty-pcm-bank.gba"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 2 && snapshot.diagnostics().empty(),
         "MP2k banks without valid DirectSound samples must not emit source-less PCM assets");
}

}  // namespace

void runMp2kModuleTests() {
  mp2kModuleBuildsAuditedSequenceAndSynth();
  mp2kDirectSoundMasterVolumeAffectsOnlyPcmVoices();
  mp2kBdpcmDecoderUsesDecodedSampleCount();
  mp2kReverseBdpcmDecodesInPlaybackOrder();
  mp2kFixedReverseDirectSoundUsesMixerRate();
  mp2kNoiseUsesAuditedRegisterClockAndWidth();
  mp2kCgbFixedToneUsesDacResolutionMask();
  mp2kCgbLengthClampsSequenceGateInPhysicalTime();
  mp2kCgbVolumeUsesCombinedHardwareQuantization();
  mp2kUndefinedJumpSlotsUseFine();
  mp2kPortConsumesItsRegisterOperands();
  mp2kUnknownMemaccDoesNotConsumeAJumpPointer();
  mp2kCompatibleDriverFallbackFindsDataTable();
  mp2kSongSelectLiteralsFindSparseTableAndRespectPlayerCapacity();
  gsfExtractorFeedsMp2kValueScanner();
  gsfExtractorZeroFillsSparseTail();
  miniGsfOverlaysLibraryAndNamesSelectedSong();
  mp2kSkipsEmptyPcmCollections();
}

void validateMp2kCorpus(const std::filesystem::path& path) {
  Session session;
  session.registerFormat(vgmtrans::formats::psf::psfExtractorDefinition());
  session.registerFormat(mp2kDefinition());
  const SourceId source = session.addSourceFromPath(path);
  ScanIdAllocator ids;
  ScanInput input{
      .source = session.sources().source(source),
      .reader = session.sources().reader(source),
      .ids = ids,
  };
  ScanResultBuilder direct(input, "MP2k");
  const size_t layoutCount = findMp2kLayouts(direct).size();
  static_cast<void>(direct.finish());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(layoutCount != 0 || snapshot.sources().size() > 1,
         "MP2k corpus file produced neither a direct nor extracted layout: " + path.string());
  std::string diagnosticText;
  for (const auto& diagnostic : snapshot.diagnostics()) {
    diagnosticText += "\n" + diagnostic.message;
  }
  expect(!snapshot.collections().empty(), "MP2k corpus file produced no collections despite finding " +
                                              std::to_string(layoutCount) + " layout(s): " + path.string() +
                                              diagnosticText);
  const CollectionPlayback playback =
      session.preparePlayback(snapshot.collections().front().id, PlaybackRequest{.sequence = {.sequenceLoops = 0}});
  expect(playback.playable(), "MP2k corpus file's first collection was not playable: " + path.string());
}

void exportMp2kCorpusSong(const std::filesystem::path& path, size_t song, const std::filesystem::path& directory) {
  Session session;
  session.registerFormat(vgmtrans::formats::psf::psfExtractorDefinition());
  session.registerFormat(mp2kDefinition());
  session.addSourceFromPath(path);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(song < snapshot.collections().size(), "MP2k export song index is outside the discovered collection list");
  const Collection& collection = snapshot.collections()[song];
  const CollectionPlayback playback =
      session.preparePlayback(collection.id, PlaybackRequest{.sequence = {.sequenceLoops = 0}});
  expect(playback.playable(), "MP2k corpus song was not playable: " + collection.name);
  std::filesystem::create_directories(directory);
  const auto write = [&](std::string_view extension, std::span<const u8> bytes) {
    std::ofstream stream(directory / (collection.name + std::string(extension)), std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  };
  write(".mid", playback.midi);
  write(".sf2", playback.soundFont);
}

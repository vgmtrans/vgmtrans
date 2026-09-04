/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HOSA/HOSA.h"
#include "value/formats/HOSA/HOSALfo.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::hosa;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
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

void initializeHeader(std::vector<u8>& bytes, u32 offset, u8 tracks) {
  std::ranges::copy(std::array<u8, 5>{'H', 'O', 'S', 'A', 'V'}, bytes.begin() + offset);
  bytes[offset + 5] = 1;
  bytes[offset + 6] = tracks;
  bytes[offset + 7] = 4;
  le16(bytes, offset + 8, 0x4000);
  le16(bytes, offset + 12, 0x3fff);
  le16(bytes, offset + 14, 0x3fff);
  for (u32 i = 0; i < 32; ++i) le16(bytes, offset + 0x10 + i * 2, static_cast<u16>((i + 1) * 24));
}

std::vector<u8> sequenceFixture(std::initializer_list<u8> commands, u32 offset = 0) {
  constexpr u16 track = 0x52;
  std::vector<u8> bytes(offset + track + commands.size(), 0);
  initializeHeader(bytes, offset, 1);
  le16(bytes, offset + 0x50, track);
  std::ranges::copy(commands, bytes.begin() + offset + track);
  return bytes;
}

std::vector<u8> multiTrackFixture(const std::vector<std::vector<u8>>& tracks) {
  const u32 data = 0x50 + static_cast<u32>(tracks.size()) * 2;
  u32 size = data;
  for (const auto& track : tracks) size += static_cast<u32>(track.size());
  std::vector<u8> bytes(size, 0);
  initializeHeader(bytes, 0, static_cast<u8>(tracks.size()));
  u32 cursor = data;
  for (u32 i = 0; i < tracks.size(); ++i) {
    le16(bytes, 0x50 + i * 2, static_cast<u16>(cursor));
    std::ranges::copy(tracks[i], bytes.begin() + cursor);
    cursor += static_cast<u32>(tracks[i].size());
  }
  return bytes;
}

template <class Event>
std::vector<const Event*> eventsOfType(const PerformanceTrack& track) {
  std::vector<const Event*> events;
  for (const auto& value : track.events) {
    if (const auto* event = std::get_if<Event>(&value)) events.push_back(event);
  }
  return events;
}

std::vector<u8> scannerFixture() {
  constexpr u32 sequence = 0x100;
  constexpr u32 decoySamples = 0x180;
  constexpr u32 samples = 0x300;
  auto bytes = sequenceFixture({0x83, 0x00, 0x21, 0xbc, 100, 0x80}, sequence);
  bytes.resize(samples + 0x80);

  // Container back-pointer and a sparse four-slot bank at +0x10.
  le32(bytes, 8, sequence);
  le32(bytes, 0x18, 4);
  le32(bytes, 0x1c, 0x20);
  le32(bytes, 0x20, 0x64);
  le32(bytes, 0x30, 4);
  bytes[0x31] = 0xaa;
  bytes[0x32] = 0xbb;
  bytes[0x33] = 0xcc;
  const u32 first = 0x34;
  le32(bytes, first, 0);
  bytes[first + 4] = 63;
  bytes[first + 5] = 60;
  bytes[first + 6] = 60;
  bytes[first + 7] = 128;
  bytes[first + 8] = 0x77;
  bytes[first + 9] = 5;
  bytes[first + 10] = 1;
  bytes[first + 11] = 0x80 | 32;
  le32(bytes, first + 12, (0x70u << 20) | (8u << 16) | (0x40u << 9) | (0x10u << 4) | 8u);
  // The driver skips these zero-threshold placeholders and falls back to
  // region zero only when no threshold contains the key.
  le32(bytes, first + 0x10, 0x20);
  le32(bytes, first + 0x20, 0x40);
  const u32 second = first + 0x30;
  le32(bytes, second, 0x60);
  bytes[second + 4] = 110;
  bytes[second + 5] = 127;
  bytes[second + 6] = 251;
  le32(bytes, second + 12, (0x60u << 20) | (6u << 16) | (0x30u << 9) | (8u << 4) | 7u);

  // A valid but incomplete pool appears first. The actual pool matches every
  // region offset and each stream follows its standard silent prefix.
  bytes[decoySamples + 0x11] = 1;
  bytes[decoySamples + 0x31] = 1;
  bytes[samples + 0x11] = 1;
  bytes[samples + 0x31] = 1;
  bytes[samples + 0x51] = 1;
  bytes[samples + 0x71] = 1;
  return bytes;
}

}  // namespace

void hosaSequencePreservesAuditedGrammarAndMixer() {
  auto bytes = sequenceFixture({
      0x81, 120,                  // tempo
      0x82, 0x33,                 // 4/4
      0x83, 0,                    // program
      0x84, 64,                   // track volume
      0x86, 32,                   // expression
      0x85, 32,                   // linear pan
      0x87, 32, 4,                // four-phase triangle auto-pan
      0x88, 32, 2, 15, 3,         // exact ten-sample driver vibrato
      0x8e, 12,                   // portamento over 12 ticks
      0x90, 0x7f,                 // dynamic attack rate
      0x21, 0xbc, 100,            // note 60, indexed duration, explicit velocity
      0xb2,                        // relative note up two, reusing timing/velocity
      0x5f, 64, 64, 0x81, 0x00,  // explicit note, delta 64, variable duration 128
      0x80,
  });
  const ByteReader reader(SourceId{81}, bytes);
  const auto layout = readSequenceLayout(reader, 0);
  expect(layout.has_value(), "HOSA sequence fixture should have a valid layout");
  const vgmtrans::formats::hosa::Region region{
      .volume = 127,
      .keyLow = 0,
      .keyHigh = 127,
      .reverb = true,
      .adsr1 = composePsxAdsr1(false, 0x20, 6, 7),
      .adsr2 = composePsxAdsr2(false, 1, 0x30, false, 8),
  };
  const SequenceProgram program =
      parseSequence(reader, AssetId{81}, *layout, {vgmtrans::formats::hosa::Instrument{.regions = {region}}});
  const auto variableNote = std::ranges::find_if(program.tracks[0].commands,
                                                  [](const SourceCommand& command) { return command.opcode == 0x5f; });
  expect(variableNote != program.tracks[0].commands.end() && variableNote->range.size == 5,
         "duration index 31 and the driver's two-byte variable encoding should stay synchronized");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.diagnostics.empty(), "audited HOSA sequence should render without diagnostics");
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(notes.size() == 3 && notes[0]->key == 60 && notes[1]->key == 62 && notes[2]->durationTicks == 128 &&
             notes[0]->linearVelocity == notes[1]->linearVelocity,
         "explicit and compressed notes should preserve driver pitch, duration, and velocity state");

  const auto levels = eventsOfType<LevelPerformanceEvent>(performance.tracks[0]);
  const auto expressions = eventsOfType<ExpressionPerformanceEvent>(performance.tracks[0]);
  expect(!levels.empty() && !expressions.empty() && std::abs(levels.back()->linearGain - 64.0 / 127.0) < 1e-12 &&
             std::abs(expressions.back()->linearGain - 32.0 / 127.0) < 1e-12,
         "track volume and expression should remain independent multiplicative lanes");
  const auto balances = eventsOfType<StereoBalancePerformanceEvent>(performance.tracks[0]);
  expect(!balances.empty() && std::abs(balances.back()->leftGain - 95.0 / 127.0) < 1e-12 &&
             std::abs(balances.back()->rightGain - 32.0 / 127.0) < 1e-12,
         "pan should use the driver's constant-sum law rather than equal-power panning");

  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(performance.tracks[0]);
  expect(envelopes.size() == 3 && envelopes[0]->scope == VoiceEnvelopeScope::FutureAttacks &&
             envelopes[1]->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks,
         "dynamic ADSR should be loaded on the next voice and should update a portamento-reused voice");
  const auto reverbs = eventsOfType<ReverbPerformanceEvent>(performance.tracks[0]);
  expect(!reverbs.empty() && std::abs(reverbs.back()->send - 0x4000 / 32767.0) < 1e-12,
         "region reverb routing should retain the HOSAV return depth");

  const auto modulation = eventsOfType<ModulationPerformanceEvent>(performance.tracks[0]);
  const auto panDepth = std::ranges::find_if(modulation, [](const auto* event) {
    return event->target == ModulationPerformanceTarget::PanDepth;
  });
  const auto vibratoDepth = std::ranges::find_if(modulation, [](const auto* event) {
    return event->target == ModulationPerformanceTarget::VibratoDepth;
  });
  expect(modulation.size() == 4 && panDepth != modulation.end() && (*panDepth)->panDepth &&
             std::abs(*(*panDepth)->panDepth - 32.0 / 127.0) < 1e-12 && vibratoDepth != modulation.end() &&
             (*vibratoDepth)->context.shape && (*vibratoDepth)->context.shape->samples.size() == 10 &&
             performance.tracks[0].automations.size() == 2,
         "auto-pan, exact fixed-table vibrato, and retrigger-safe portamento should retain physical intent");
}

void hosaVibratoUsesExactDriverTables() {
  constexpr std::array<std::size_t, 16> lengths{8,  2,  7,   3,  7,   3,   156, 39,
                                                64, 16, 80, 20, 256, 256, 40,  10};
  for (u8 waveform = 0; waveform < lengths.size(); ++waveform) {
    const LfoShape shape = vibratoShape(waveform);
    expect(shape.samples.size() == lengths[waveform],
           "each HOSA vibrato selector should use its exact driver-table length");
  }
  const LfoShape rampedSquare = vibratoShape(0);
  const LfoShape noise = vibratoShape(12);
  expect(std::abs(rampedSquare.samples.front() - 8191.0 / 32768.0) < 1e-12 &&
             std::abs(rampedSquare.samples.back() + 32767.0 / 32768.0) < 1e-12 &&
             std::abs(noise.samples[1] - 12797.0 / 32768.0) < 1e-12,
         "HOSA LFO samples should retain the signed 16-bit values from the driver executable");
}

void hosaUnterminatedFinalTrackStopsAtZeroPadding() {
  constexpr u32 track = 0x52;
  auto bytes = sequenceFixture({
      0x21, 0xbc, 127,  // ordinary note with explicit velocity
      0x21, 0x00,       // an isolated key-zero note remains valid
  });
  bytes.resize(track + 5 + 64, 0);
  bytes.back() = 0x80;

  const ByteReader reader(SourceId{82}, bytes);
  const auto layout = readSequenceLayout(reader, 0);
  expect(layout.has_value() && layout->tracks.size() == 1 && layout->tracks[0].end == track + 5 &&
             layout->length == track + 5,
         "an unterminated final track should stop at zero padding instead of scanning unrelated memory");

  const SequenceProgram program = parseSequence(reader, AssetId{82}, *layout);
  expect(program.tracks.size() == 1 && program.tracks[0].commands.size() == 2,
         "zero padding should not produce phantom sequence commands");
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->key == 60 && notes[1]->key == 0,
         "padding detection should not reject an isolated key-zero note");
}

void hosaTracksMayShareSequenceData() {
  constexpr u16 first = 0x54;
  constexpr u16 second = 0x55;
  std::vector<u8> bytes(second + 4, 0);
  initializeHeader(bytes, 0, 2);
  le16(bytes, 0x50, first);
  le16(bytes, 0x52, second);
  std::ranges::copy(std::array<u8, 5>{0xb1, 0x21, 0xbc, 100, 0x80}, bytes.begin() + first);

  const ByteReader reader(SourceId{83}, bytes);
  const auto layout = readSequenceLayout(reader, 0);
  expect(layout && layout->tracks[0].end == first + 5 && layout->tracks[1].end == first + 5,
         "track pointers should be starts, allowing an earlier track to join a later track's stream");
  const SequenceProgram program = parseSequence(reader, AssetId{83}, *layout);
  expect(program.tracks[0].commands.size() == 3 && program.tracks[1].commands.size() == 2,
         "both tracks should decode their complete shared suffix");
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(notes.size() == 2 && notes[0]->key == 0 && notes[0]->linearVelocity == 0.0 &&
             notes[0]->durationTicks == 0xff,
         "the initial note, velocity, and duration state should match the driver's zero/0xff defaults");
}

void hosaSequenceLoopRestoresEveryTrack() {
  auto bytes = multiTrackFixture({
      {
          0x20, 0xbc, 127,  // 24-tick note before the loop
          0xc9, 0, 0,       // save the global checkpoint at tick 24 with zero delta
          0x20, 0xbc, 127,  // 24-tick loop note
          0x89, 1,          // restore all tracks and release every voice
          0x80,
      },
      {
          0xc4, 32, 24,          // volume 32, wait to the global checkpoint
          0x41, 0xc1, 12, 127,   // key 65, delta 12, duration 48
          0xc4, 10, 12,          // mutate volume inside the loop
          0x80,
      },
  });
  const ByteReader reader(SourceId{84}, bytes);
  const auto layout = readSequenceLayout(reader, 0);
  expect(layout.has_value(), "synchronized HOSA loop fixture should have a valid layout");
  const SequenceProgram program = parseSequence(reader, AssetId{84}, *layout);
  const PerformanceSequence performance =
      SequenceVm(SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 1}).render(program);
  expect(performance.diagnostics.empty(), "synchronized HOSA loop should render without diagnostics");

  const auto loopNotes = eventsOfType<NotePerformanceEvent>(performance.tracks[1]);
  expect(loopNotes.size() == 2 && loopNotes[0]->header.tick == 24 && loopNotes[1]->header.tick == 48 &&
             loopNotes[0]->durationTicks == 24 && loopNotes[1]->durationTicks == 24,
         "the loop coordinator should restore every track and release notes at each global boundary");
  const auto levels = eventsOfType<LevelPerformanceEvent>(performance.tracks[1]);
  const auto restored = std::ranges::find_if(levels, [](const LevelPerformanceEvent* event) {
    return event->header.tick == 48 && std::abs(event->linearGain - 32.0 / 127.0) < 1e-12;
  });
  expect(restored != levels.end(),
         "sequence-loop restoration should recover controller state, not merely rewind source pointers");
}

void hosaModuleBuildsDriverAccurateRegions() {
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "HOSA fixture.bin"}, scannerFixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "HOSA fixture should produce one collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.soundBanks.size() == 1 && collection.members.sequence,
         "HOSA collection should bind its HOSAV sequence and local bank");
  const auto* bank = snapshot.asset<SoundBankAsset>(collection.members.soundBanks.front());
  expect(bank != nullptr && bank->instruments.size() == 1 && bank->instruments[0].regions.size() == 2 &&
             bank->localSamples.samples.size() == 2 && bank->instruments[0].reverb == 0.0,
         "bank pointers, driver region thresholds, and sample offsets should resolve the complete local synth");
  expect(bank->localSamples.samples[0].encodedData.offset == 0x300,
         "sample discovery should select the pool matching every bank offset rather than the first valid pool");
  const vgmtrans::core::Region& region = bank->instruments[0].regions[0];
  expect(region.keyRange.low == 0 && region.keyRange.high == 60 && std::abs(region.unityKey - 59.5) < 1e-12 &&
             std::abs(region.attenuationDb - 6.089263430) < 0.000001,
         "region key ranges, 1/256-semitone tuning, and 0..127 volume should follow the driver");
  const vgmtrans::core::Region& wrappedRegion = bank->instruments[0].regions[1];
  expect(std::abs(wrappedRegion.unityKey - 125.0) < 1e-12,
         "region tuning should preserve the driver's seven-bit pitch wrap");
  expect(std::abs(std::pow(10.0, -wrappedRegion.attenuationDb / 20.0) - 110.0 / 127.0) < 1e-12,
         "the Serene Town drum-region volume should remain a linear 110/127 amplitude factor");
}

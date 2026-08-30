/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OhoriAkaPS1/OhoriAkaPS1.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::ohori_aka_ps1;

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

std::vector<u8> sequenceFixture(std::initializer_list<u8> commands, u32 offset = 0) {
  constexpr u16 track = 0x52;
  std::vector<u8> bytes(offset + track + commands.size(), 0);
  std::ranges::copy(std::array<u8, 5>{'H', 'O', 'S', 'A', 'V'}, bytes.begin() + offset);
  bytes[offset + 5] = 1;
  bytes[offset + 6] = 1;
  le16(bytes, offset + 12, 0x3fff);
  le16(bytes, offset + 14, 0x3fff);
  for (u32 i = 0; i < 32; ++i) le16(bytes, offset + 0x10 + i * 2, static_cast<u16>((i + 1) * 24));
  le16(bytes, offset + 0x50, track);
  std::ranges::copy(commands, bytes.begin() + offset + track);
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

  // Container back-pointer and its one-instrument bank at +0x10.
  le32(bytes, 8, sequence);
  le32(bytes, 0x18, 1);
  le32(bytes, 0x1c, 0x10);
  le32(bytes, 0x20, 4);
  const u32 first = 0x24;
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
  // The driver ignores these zero-threshold placeholders: region selection is
  // the first high threshold that contains the key, falling back to region 0.
  le32(bytes, first + 0x10, 0x20);
  le32(bytes, first + 0x20, 0x40);
  const u32 second = first + 0x30;
  le32(bytes, second, 0x60);
  bytes[second + 4] = 110;
  bytes[second + 5] = 127;
  bytes[second + 6] = 251;
  le32(bytes, second + 12, (0x60u << 20) | (6u << 16) | (0x30u << 9) | (8u << 4) | 7u);

  // A real but incomplete pool appears first. The bank's actual pool matches
  // every region offset, and each stream has the standard silent prefix.
  bytes[decoySamples + 0x11] = 1;
  bytes[decoySamples + 0x31] = 1;
  bytes[samples + 0x11] = 1;
  bytes[samples + 0x31] = 1;
  bytes[samples + 0x51] = 1;
  bytes[samples + 0x71] = 1;
  return bytes;
}

}  // namespace

void ohoriAkaPs1SequencePreservesAuditedGrammarAndMixer() {
  auto bytes = sequenceFixture({
      0x81, 120,              // tempo
      0x82, 0x33,             // 4/4
      0x83, 0,                // program
      0x84, 64,               // track volume
      0x86, 32,               // expression
      0x85, 32,               // linear pan
      0x87, 32, 4,            // four-phase triangle auto-pan
      0x88, 32, 2, 9, 3,      // fixed-table vibrato
      0x8e, 12,               // portamento over 12 ticks
      0x90, 0x7f,             // dynamic attack rate
      0x21, 0xbc, 100,        // note 60, indexed duration, explicit velocity
      0xb2,                    // relative note up two, reusing timing/velocity
      0x5f, 64, 64, 0x81, 0x00,  // explicit note, delta 64, variable duration 128
      0x80,
  });
  const ByteReader reader(SourceId{81}, bytes);
  const auto layout = readOhoriAkaPs1SequenceLayout(reader, 0);
  expect(layout.has_value(), "OhoriAkaPS1 sequence fixture should have a valid layout");
  const OhoriAkaPs1Region region{
      .volume = 127,
      .keyLow = 0,
      .keyHigh = 127,
      .adsr1 = composePsxAdsr1(false, 0x20, 6, 7),
      .adsr2 = composePsxAdsr2(false, 1, 0x30, false, 8),
  };
  const SequenceProgram program = parseOhoriAkaPs1Sequence(
      reader, AssetId{81}, *layout, {OhoriAkaPs1Instrument{.program = 0, .regions = {region}}});
  const auto variableNote = std::ranges::find_if(program.tracks[0].commands,
                                                  [](const SourceCommand& command) { return command.opcode == 0x5f; });
  expect(variableNote != program.tracks[0].commands.end() && variableNote->range.size == 5,
         "duration index 31 and the driver's two-byte variable encoding should stay synchronized");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.diagnostics.empty(), "OhoriAkaPS1 audited sequence should render without diagnostics");
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(notes.size() == 3 && notes[0]->key == 60 && notes[1]->key == 62 && notes[2]->durationTicks == 128 &&
             notes[0]->linearVelocity == notes[1]->linearVelocity,
         "explicit and compressed notes should preserve driver pitch, duration, and velocity state");
  const auto levels = eventsOfType<LevelPerformanceEvent>(performance.tracks[0]);
  expect(!levels.empty() && std::abs(levels.back()->linearGain - (64.0 / 127.0) * (32.0 / 127.0)) < 1e-12,
         "track volume and expression should multiply as independent linear lanes");
  const auto balances = eventsOfType<StereoBalancePerformanceEvent>(performance.tracks[0]);
  expect(!balances.empty() && std::abs(balances.back()->leftGain - 95.0 / 127.0) < 1e-12 &&
             std::abs(balances.back()->rightGain - 32.0 / 127.0) < 1e-12,
         "pan should use the driver's constant-sum law rather than equal-power panning");
  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(performance.tracks[0]);
  expect(!envelopes.empty() && envelopes.back()->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks,
         "dynamic ADSR writes should affect active and future Ohori driver voices");
  const auto modulation = eventsOfType<ModulationPerformanceEvent>(performance.tracks[0]);
  expect(modulation.size() == 4 && performance.tracks[0].automations.size() >= 2 &&
             std::ranges::all_of(performance.tracks[0].automations,
                                 [](const PerformanceAutomation& automation) {
                                   return std::holds_alternative<PitchTransitionIntent>(automation.intent);
                                 }),
         "auto-pan, fixed-table vibrato, and timed native portamento should survive as physical playback intent");
}

void ohoriAkaPs1UnterminatedFinalTrackStopsAtZeroPadding() {
  constexpr u32 track = 0x52;
  auto bytes = sequenceFixture({
      0x21, 0xbc, 127,  // ordinary note with explicit velocity
      0x21, 0x00,       // an isolated key-zero note remains valid
  });
  bytes.resize(track + 5 + 64, 0);
  bytes.back() = 0x80;  // unrelated data must not become the track terminator

  const ByteReader reader(SourceId{82}, bytes);
  const auto layout = readOhoriAkaPs1SequenceLayout(reader, 0);
  expect(layout.has_value() && layout->trackEnds.size() == 1 && layout->trackEnds[0] == track + 5 &&
             layout->length == track + 5,
         "an unterminated final track should stop at zero padding instead of scanning unrelated memory");

  const SequenceProgram program = parseOhoriAkaPs1Sequence(reader, AssetId{82}, *layout);
  expect(program.tracks.size() == 1 && program.tracks[0].commands.size() == 2,
         "zero padding should not produce phantom sequence commands");
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->key == 60 && notes[1]->key == 0,
         "padding detection should not reject an isolated key-zero note");
}

void ohoriAkaPs1ModuleBuildsDriverAccurateRegions() {
  Session session;
  session.registerFormat(ohoriAkaPs1Module());
  session.addSource(SourceFile{.name = "OhoriAkaPS1 fixture.bin"}, scannerFixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "OhoriAkaPS1 fixture should produce one collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.soundBanks.size() == 1 && collection.members.sequence,
         "OhoriAkaPS1 collection should bind its HOSAV sequence and local bank");
  const auto* bank = snapshot.asset<SoundBankAsset>(collection.members.soundBanks.front());
  expect(bank != nullptr && bank->instruments.size() == 1 && bank->instruments[0].regions.size() == 2 &&
             bank->localSamples.samples.size() == 2,
         "bank pointers, driver region thresholds, and sample offsets should resolve the complete local synth");
  expect(bank->localSamples.samples[0].encodedData.offset == 0x300,
         "sample discovery should select the pool matching every bank offset rather than the first valid pool");
  const Region& region = bank->instruments[0].regions[0];
  expect(region.keyRange.low == 0 && region.keyRange.high == 60 && std::abs(region.unityKey - 59.5) < 1e-12 &&
             std::abs(region.attenuationDb - 6.089263430) < 0.000001,
         "region key ranges, 1/256-semitone tuning, and 0..127 volume should follow the driver");
  const Region& wrappedRegion = bank->instruments[0].regions[1];
  expect(std::abs(wrappedRegion.unityKey - 125.0) < 1e-12,
         "region tuning should preserve the driver's seven-bit pitch wrap");
  expect(std::abs(std::pow(10.0, -wrappedRegion.attenuationDb / 20.0) - 110.0 / 127.0) < 1e-12,
         "the Serene Town drum-region volume should remain a linear 110/127 amplitude factor");
}

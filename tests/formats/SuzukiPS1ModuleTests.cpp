/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiPS1/SuzukiPS1.h"

#include "value/session/Session.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::suzuki_ps1;

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

std::vector<u8> sequenceFixture(std::initializer_list<u8> commands, u16 bank = 3) {
  constexpr u32 track = 0x26;
  std::vector<u8> bytes(track + commands.size(), 0);
  bytes[0] = 's';
  bytes[1] = 'm';
  bytes[2] = 'd';
  bytes[3] = 's';
  le16(bytes, 0x08, static_cast<u16>(bytes.size()));
  bytes[0x14] = 1;
  le16(bytes, 0x16, bank);
  le16(bytes, 0x1e, 0x24);
  le16(bytes, 0x20, 0x25);
  le16(bytes, 0x22, track);
  std::ranges::copy(commands, bytes.begin() + track);
  return bytes;
}

std::vector<u8> scannerFixture() {
  constexpr u32 bankOffset = 0x100;
  constexpr u32 headerSize = 0x40;
  constexpr u32 sampleSize = 0x20;
  auto bytes = sequenceFixture({0xac, 0x00, 100, 1, 0x90});
  bytes.resize(bankOffset + headerSize + sampleSize);
  bytes[bankOffset] = 'd';
  bytes[bankOffset + 1] = 'w';
  bytes[bankOffset + 2] = 'd';
  bytes[bankOffset + 3] = 's';
  le32(bytes, bankOffset + 0x08, headerSize + sampleSize);
  le32(bytes, bankOffset + 0x10, headerSize);
  le32(bytes, bankOffset + 0x14, sampleSize);
  le32(bytes, bankOffset + 0x1c, 0);
  le32(bytes, bankOffset + 0x20, 3);

  const u32 instrument = bankOffset + 0x30;
  le32(bytes, instrument, 0x10);
  bytes[instrument + 6] = 0x80;
  bytes[instrument + 7] = 0xff;
  bytes[instrument + 8] = 0x70;
  bytes[instrument + 9] = 8;
  bytes[instrument + 10] = 0x40;
  bytes[instrument + 11] = 0x10;
  bytes[instrument + 12] = 8;
  bytes[instrument + 13] = 5;
  bytes[instrument + 14] = 7;
  bytes[instrument + 15] = 5;

  // The bank begins with one silent upload block; program zero points at the
  // following one-block sample.
  bytes[bankOffset + headerSize + 0x10 + 1] = 1;
  return bytes;
}

template <class Event>
std::vector<const Event*> eventsOfType(const PerformanceTrack& track) {
  std::vector<const Event*> events;
  for (const auto& value : track.events) {
    if (const auto* event = std::get_if<Event>(&value)) {
      events.push_back(event);
    }
  }
  return events;
}

}  // namespace

void suzukiPs1DynamicAdsrUsesAuditedDriverCommands() {
  auto bytes = sequenceFixture({
      0xac, 0x00,              // select the native envelope below
      0xc2, 0x7f,              // attack rate
      0xc3, 0x0f,              // decay rate (missing in the legacy decoder)
      0xc4, 0x40,              // sustain rate
      0xc5, 0x10,              // release rate
      0xc6, 0x08,              // sustain level
      0xc7, 0x04, 0x09,        // decay plus sustain level
      0xc8, 0x05,              // attack mode
      0xc9, 0x07,              // sustain mode
      0xca, 0x05,              // release mode
      0xc1, 0x05, 0x07, 0x05,  // all three modes
      0xfe, 0x04,              // select another WDS without loading it
      0xc0,                    // load that bank's program and ADSR
      100,  1,                 // C3, duration 192
      0x90,
  });
  const ByteReader reader(SourceId{71}, bytes);
  const auto layout = readSuzukiPs1SequenceLayout(reader, 0);
  expect(layout.has_value(), "SuzukiPS1 sequence fixture should have a valid layout");

  const u16 initialAdsr1 = composePsxAdsr1(0, 0x20, 6, 7);
  const u16 initialAdsr2 = composePsxAdsr2(0, 1, 0x30, 0, 8);
  const SequenceProgram program = parseSuzukiPs1Sequence(
      reader, AssetId{71}, *layout,
      {
          SuzukiPs1Instrument{.bank = 3, .program = 0, .adsr1 = initialAdsr1, .adsr2 = initialAdsr2},
          SuzukiPs1Instrument{.bank = 4, .program = 0, .adsr1 = initialAdsr1, .adsr2 = initialAdsr2},
      });
  const TrackProgram& track = program.tracks.front();
  const auto c1 =
      std::ranges::find_if(track.commands, [](const SourceCommand& command) { return command.opcode == 0xc1; });
  const auto c3 =
      std::ranges::find_if(track.commands, [](const SourceCommand& command) { return command.opcode == 0xc3; });
  expect(c1 != track.commands.end() && c1->range.size == 4 && c3 != track.commands.end() && c3->range.size == 2,
         "audited C1 and C3 operand lengths should keep the stream synchronized");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.diagnostics.empty(), "SuzukiPS1 ADSR fixture should render without diagnostics");
  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(performance.tracks.front());
  expect(envelopes.size() == 12,
         "program selection, ten dynamic writes, and ADSR reset should each expose their envelope effect");
  expect(std::ranges::all_of(envelopes,
                             [](const EnvelopePerformanceEvent* event) {
                               return event->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks;
                             }),
         "SuzukiPS1 dynamic ADSR writes should affect active voices and future attacks");
  expect(envelopes.back()->update.values == std::nullopt && envelopes.back()->update.fields == EnvelopeFields::All,
         "C0 should restore the current program's native envelope");
  const auto instruments = eventsOfType<InstrumentPerformanceEvent>(performance.tracks.front());
  expect(!instruments.empty() && instruments.back()->sourceInstrument == suzukiPs1InstrumentIdentity(4, 0),
         "C0 should load the current program from a bank selected by FE");
}

void suzukiPs1ModuleBuildsFractionallyTunedWdsSynth() {
  Session session;
  session.registerFormat(suzukiPs1Module());
  session.addSource(SourceFile{.name = "SuzukiPS1 fixture.bin"}, scannerFixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "SuzukiPS1 fixture should produce one collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.sequence.has_value() && collection.members.soundBanks.size() == 1 &&
             collection.members.samplePools.empty(),
         "SuzukiPS1 collection should connect its sequence and self-contained WDS sound bank");

  const auto* instruments = snapshot.asset<SoundBankAsset>(collection.members.soundBanks.front());
  expect(instruments != nullptr && instruments->instruments.size() == 1 &&
             instruments->instruments.front().identity == suzukiPs1InstrumentIdentity(3, 0) &&
             instruments->instruments.front().regions.size() == 1,
         "WDS instruments should retain their source bank and program identity");
  const Region& region = instruments->instruments.front().regions.front();
  expect(std::abs(region.unityKey - 60.5) < 0.000001,
         "WDS semitone and 1/256-semitone tuning should remain fractional");
  expect(region.envelope == psxSpuEnvelope(composePsxAdsr1(1, 0x70, 8, 8), composePsxAdsr2(1, 1, 0x40, 1, 0x10)),
         "DWDS mode bytes should feed the exact native PSX ADSR conversion");
}

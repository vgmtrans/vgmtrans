/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatPS1/HeartBeatPS1.h"

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
using namespace vgmtrans::formats::heartbeat_ps1;

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

void be16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value >> 8);
  bytes[offset + 1] = static_cast<u8>(value);
}

std::vector<u8> heartBeatFixture() {
  constexpr u32 headerSize = 0x3c;
  constexpr u32 sampleSize = 0x20;
  constexpr u32 attributeSize = 8 + 0x24 + 0x14;
  const std::vector<u8> events{
      0,    0xb0, 32,   0,    0,    0xc0, 0,  0, 0xb0, 56, 2,  0, 0xb0, 76, 63, 0, 0xb0, 77, 16,  0, 0xb0, 78, 1,   0,
      0xb0, 52,   2,    0,    0xb0, 54,   63, 0, 0xb0, 55, 32, 0, 0xb0, 92, 1,  0, 0xb0, 73, 100, 0, 0xb0, 72, 20,  0,
      0xb0, 74,   60,   0,    0xb0, 79,   8,  0, 0xb0, 22, 50, 0, 0xb0, 91, 1,  0, 0xb0, 5,  121, 0, 0x90, 60, 100, 0,
      0xe0, 0x7f, 0x7f, 10,   0x80, 60,   0,  0, 0xb0, 99, 20, 0, 0xb0, 6,  2,  0, 0x90, 62, 100, 1, 0x80, 62, 0,   0,
      0xb0, 99,   30,   0xff, 0x2f, 0,
  };
  const u32 sequenceSize = 0x10 + static_cast<u32>(events.size());
  const u32 sampleOffset = headerSize;
  const u32 attributeOffset = sampleOffset + sampleSize;
  const u32 qQesOffset = attributeOffset + attributeSize;
  std::vector<u8> bytes(qQesOffset + sequenceSize, 0);

  le32(bytes, 0, sequenceSize);
  le16(bytes, 4, 7);
  bytes[6] = 4;
  bytes[7] = 4;
  le32(bytes, 0x0c, sampleSize);
  le32(bytes, 0x10, attributeSize);
  le16(bytes, 0x14, 1);
  for (u32 index = 1; index < 4; ++index) {
    le16(bytes, 0x0c + index * 0x0c + 8, 0xffff);
  }

  // One valid single-block PSX ADPCM stream.
  bytes[sampleOffset + 1] = 1;

  bytes[attributeOffset + 1] = 1;
  le16(bytes, attributeOffset + 2, 1);
  bytes[attributeOffset + 4] = 127;
  bytes[attributeOffset + 5] = 64;
  bytes[attributeOffset + 6] = 0x76;
  const u32 program = attributeOffset + 8;
  for (u32 index = 0; index < 16; ++index) {
    le16(bytes, program + index * 2, 0xffff);
  }
  le16(bytes, program, 0);
  bytes[program + 0x20] = 127;
  bytes[program + 0x21] = 64;

  const u32 tone = program + 0x24;
  le32(bytes, tone, 0);
  le16(bytes, tone + 4, composePsxAdsr1(1, 0x60, 8, 8));
  le16(bytes, tone + 6, composePsxAdsr2(1, 1, 0x40, 1, 0x10));
  bytes[tone + 9] = 127;
  bytes[tone + 10] = 64;
  bytes[tone + 11] = 60;
  bytes[tone + 12] = 64;
  bytes[tone + 13] = 6;
  bytes[tone + 14] = 12;
  bytes[tone + 15] = 0;
  bytes[tone + 16] = 127;
  bytes[tone + 17] = 4;

  bytes[qQesOffset] = 'q';
  bytes[qQesOffset + 1] = 'Q';
  bytes[qQesOffset + 2] = 'E';
  bytes[qQesOffset + 3] = 'S';
  be16(bytes, qQesOffset + 6, 0x1001);
  be16(bytes, qQesOffset + 8, 480);
  bytes[qQesOffset + 10] = 0x07;
  bytes[qQesOffset + 11] = 0xa1;
  bytes[qQesOffset + 12] = 0x20;
  bytes[qQesOffset + 13] = 4;
  bytes[qQesOffset + 14] = 2;
  bytes[qQesOffset + 15] = 1;
  std::ranges::copy(events, bytes.begin() + qQesOffset + 0x10);
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

void heartBeatPs1SequenceModelsAuditedDriverFeatures() {
  const auto bytes = heartBeatFixture();
  const ByteReader reader(SourceId{91}, bytes);
  const auto container = readHeartBeatPs1Container(reader, 0);
  expect(container && container->sequence && container->banks.size() == 1,
         "HeartBeatPS1 fixture should expose its bank and qQES sequence");
  const auto& layout = *container->sequence;
  const auto enable = std::ranges::find_if(
      layout.events, [](const auto& event) { return (event.status & 0xf0) == 0xb0 && event.data1 == 78; });
  expect(enable != layout.events.end(), "vibrato-enable should remain synchronized in the interleaved stream");
  expect(std::ranges::any_of(layout.events, [](const auto& event) { return event.loopDestination.has_value(); }),
         "CC99/6/99 should recover the driver's NRPN loop destination");

  HeartBeatPs1Tone tone{.bendDownSemitones = 6, .bendUpSemitones = 12, .keys = KeyRange{.low = 0, .high = 127}};
  const SequenceProgram program = parseHeartBeatPs1Sequence(
      reader, AssetId{91}, layout, {HeartBeatPs1InstrumentInfo{.bank = 1, .program = 0, .tones = {tone}}});

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.diagnostics.empty(), "HeartBeatPS1 feature fixture should render without diagnostics");
  const auto modulation = eventsOfType<ModulationPerformanceEvent>(performance.tracks.front());
  expect(std::ranges::any_of(modulation,
                             [](const auto* event) {
                               return event->target == ModulationPerformanceTarget::VibratoDepth &&
                                      event->pitchDepthSemitones && *event->pitchDepthSemitones > 0.0;
                             }) &&
             std::ranges::any_of(modulation,
                                 [](const auto* event) {
                                   return event->target == ModulationPerformanceTarget::TremoloDepth &&
                                          event->volumeDepthLinearGain && *event->volumeDepthLinearGain > 0.0;
                                 }),
         "CC56/76/77/78 and CC52-55/92 should emit physical vibrato and tremolo");
  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(performance.tracks.front());
  expect(envelopes.size() == 4 &&
             std::ranges::all_of(envelopes,
                                 [](const auto* event) { return event->scope == VoiceEnvelopeScope::FutureAttacks; }),
         "dynamic SPU ADSR writes should affect future attacks, exactly like the driver");
  const auto bends = eventsOfType<PitchBendPerformanceEvent>(performance.tracks.front());
  expect(!bends.empty() && std::abs(bends.back()->semitones - 12.0) < 0.001,
         "pitch bend should use the selected tone's asymmetric upward range");
  expect(eventsOfType<ReverbPerformanceEvent>(performance.tracks.front()).size() == 2,
         "global wet depth and future-voice reverb routing should both remain audible");
}

void heartBeatPs1ModuleBuildsEmbeddedWaveBank() {
  Session session;
  session.registerFormat(heartBeatPs1Module());
  session.addSource(SourceFile{.name = "HeartBeatPS1 fixture.bin"}, heartBeatFixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "HeartBeatPS1 fixture should produce one collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.sequence && collection.members.soundBanks.size() == 1,
         "the collection should connect qQES playback to its embedded wave bank");
  const auto* bank = snapshot.asset<SoundBankAsset>(collection.members.soundBanks.front());
  expect(bank && bank->instruments.size() == 1 && bank->localSamples.samples.size() == 1,
         "the embedded attribute and ADPCM sections should become one playable sound bank");
  const Instrument& instrument = bank->instruments.front();
  expect(instrument.identity == heartBeatPs1InstrumentIdentity(1, 0) && instrument.regions.size() == 1,
         "program identity and its tone region should retain the source wave-bank ID");
  expect(std::abs(instrument.regions.front().unityKey - 59.5) < 0.000001 && instrument.reverb == 1.0,
         "fractional root tuning and tone-default reverb routing should survive synth construction");
}

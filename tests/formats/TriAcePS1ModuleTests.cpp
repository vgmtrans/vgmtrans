/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TriAcePS1/TriAcePS1.h"

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
using namespace vgmtrans::formats::triace_ps1;

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

std::vector<u8> slzWithImplicitTail(std::vector<u8> decoded) {
  constexpr u32 omitted = 3;
  decoded.resize(decoded.size() + omitted, 0);
  le16(decoded, 2, static_cast<u16>(decoded.size() - 2));

  std::vector<u8> payload;
  for (size_t cursor = 0; cursor < decoded.size() - omitted;) {
    const size_t count = std::min<size_t>(8, decoded.size() - omitted - cursor);
    payload.push_back(static_cast<u8>((1u << count) - 1));
    payload.insert(payload.end(), decoded.begin() + static_cast<std::ptrdiff_t>(cursor),
                   decoded.begin() + static_cast<std::ptrdiff_t>(cursor + count));
    cursor += count;
    if (count < 8) {
      payload.insert(payload.end(), {0, 0});
    }
  }
  if ((decoded.size() - omitted) % 8 == 0) {
    payload.insert(payload.end(), {0, 0, 0});
  }

  std::vector<u8> slz(16 + payload.size(), 0);
  slz[0] = 'S';
  slz[1] = 'L';
  slz[2] = 'Z';
  slz[3] = 1;
  le32(slz, 8, static_cast<u32>(decoded.size()));
  le32(slz, 12, static_cast<u32>(slz.size()));
  std::ranges::copy(payload, slz.begin() + 16);
  return slz;
}

std::vector<u8> sequenceFixture(const std::vector<std::vector<u8>>& patterns) {
  constexpr u16 playlist = 0xd6;
  std::vector<u8> bytes(playlist + (patterns.size() + 1) * 2, 0);
  le16(bytes, 0, 0xffff);
  bytes[0x0f] = 80;
  bytes[0x10] = 4;
  bytes[0x11] = 4;
  le16(bytes, 0x16 + 4, playlist);
  for (size_t index = 0; index < patterns.size(); ++index) {
    le16(bytes, playlist + index * 2, static_cast<u16>(bytes.size()));
    bytes.insert(bytes.end(), patterns[index].begin(), patterns[index].end());
  }
  le16(bytes, playlist + patterns.size() * 2, 0xffff);
  le16(bytes, 2, static_cast<u16>(bytes.size() - 2));
  return bytes;
}

std::vector<u8> scannerFixture() {
  const auto slz = slzWithImplicitTail(sequenceFixture({{
      0x83,
      0,
      0x12,
      3,
      0x9e,
      4,
      100,
      60,
      4,
      0x80,
  }}));
  constexpr u32 bankOffset = 0x1000;
  constexpr u32 instrumentSectionSize = 44;
  constexpr u32 sampleSize = 0x100;
  std::vector<u8> bytes(bankOffset + instrumentSectionSize + sampleSize, 0);
  std::ranges::copy(slz, bytes.begin());

  le32(bytes, bankOffset, instrumentSectionSize + sampleSize);
  le16(bytes, bankOffset + 4, instrumentSectionSize);
  const u32 instrument = bankOffset + 12;
  bytes[instrument] = 0x12;
  bytes[instrument + 1] = 3;
  le16(bytes, instrument + 2, 0x8f3f);
  le16(bytes, instrument + 4, 0x5fc8);
  bytes[instrument + 7] = 1;
  const u32 region = instrument + 8;
  bytes[region + 1] = 127;
  bytes[region + 3] = 127;
  bytes[region + 12] = 127;
  le32(bytes, region + 16, 0x4000);
  le32(bytes, bankOffset + instrumentSectionSize - 4, 0xffffffff);
  bytes[bankOffset + instrumentSectionSize + 16 + 1] = 1;
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

void triAcePs1SequenceExecutesAuditedDriverFeatures() {
  const auto bytes = sequenceFixture({
      {
          0x83, 0, 0x12, 3,                 // native instrument and ADSR
          0x94, 0, 2,    4,    8,           // automatic vibrato parameters
          0x93, 0, 1,                       // automatic vibrato on
          0x90, 0, 1,                       // per-track reverb send
          0x8a, 0, 0x40,                    // global reverb depth
          0x91, 0, 1,                       // invert reverb left
          0x97, 0, 0x34, 0x12, 0x78, 0x56,  // dynamic native ADSR
          0x96, 0, 4,                       // bend range
          0x84, 0, 32,                      // +2 semitones
          0x9e, 2, 100,                     // both note fields implied
          0x8d,                             // repeat begin
          0x80,
      },
      {
          60,
          1,
          2,
          100,  // repeated note; each pattern restores explicit note fields
          0x80,
      },
      {
          0x8e,
          2,  // repeat end is deliberately in another playlist pattern
          61,
          1,
          2,
          100,  // pattern end restores explicit duration and velocity
          0x89,
          3,
          1,  // hold the note past its scheduled end
          0x89,
          0,
          0,
          0x80,
      },
  });
  const ByteReader reader(SourceId{91}, bytes);
  const auto layout = readTriAcePs1SequenceLayout(reader, 0);
  expect(layout.has_value(), "TriAcePS1 sequence fixture should have a valid layout");
  const SequenceProgram program = parseTriAcePs1Sequence(reader, AssetId{91}, *layout);
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.diagnostics.empty(), "audited TriAcePS1 fixture should render without diagnostics");
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = eventsOfType<NotePerformanceEvent>(track);
  expect(notes.size() == 3 && notes[0]->key == 60.0 && notes[1]->key == 60.0 && notes[2]->key == 61.0,
         "repeat flow and the pattern playlist should render all three notes (notes=" + std::to_string(notes.size()) +
             ")");
  expect(notes[1]->extendsPrevious && notes[2]->durationTicks == 4,
         "active same-key notes should extend without another attack, and sustain should defer their release");

  const auto modulation = eventsOfType<ModulationPerformanceEvent>(track);
  const auto depth = std::ranges::find_if(modulation, [](const ModulationPerformanceEvent* event) {
    return event->target == ModulationPerformanceTarget::VibratoDepth && event->pitchDepthSemitones == 0.0 &&
           event->context.delayTicks == 2;
  });
  expect(depth != modulation.end() && (*depth)->context.cyclesPerTick == 0.125 && (*depth)->context.delayTicks == 2 &&
             (*depth)->context.shape && (*depth)->context.shape->samples.size() == 64,
         "automatic vibrato should retain the audited waveform, rate, and delay");
  const auto ramp = std::ranges::find_if(track.automations, [](const PerformanceAutomation& automation) {
    const auto* intent = std::get_if<ScalarPerformanceAutomationIntent>(&automation.intent);
    return intent != nullptr && intent->target == PerformanceAutomationTarget::VibratoDepth;
  });
  const auto* rampIntent =
      ramp == track.automations.end() ? nullptr : std::get_if<ScalarPerformanceAutomationIntent>(&ramp->intent);
  expect(rampIntent != nullptr && rampIntent->motion == PerformanceAutomationMotion::Envelope &&
             rampIntent->targetValue == 4.0 && rampIntent->durationTicks == 4 && rampIntent->delayTicks == 2 &&
             rampIntent->restartsOnNote,
         "automatic vibrato depth should ramp once per music tick after the note delay");

  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(track);
  expect(envelopes.size() >= 2 && envelopes.back()->update.values == psxSpuEnvelope(0x1234, 0x5678) &&
             envelopes.back()->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks,
         "opcode 97 should replace exact native ADSR on active and future voices");
  const auto bends = eventsOfType<PitchBendPerformanceEvent>(track);
  expect(bends.size() == 1 && bends.front()->semitones == 2.0,
         "pitch bend should use the current driver range and signed 1/64 scaling");
  const auto reverbs = eventsOfType<ReverbPerformanceEvent>(track);
  const auto invertedReverb = std::ranges::find_if(reverbs, [](const ReverbPerformanceEvent* event) {
    return event->leftGain && *event->leftGain < 0.0 && event->rightGain && *event->rightGain > 0.0;
  });
  expect(reverbs.size() >= 3 && invertedReverb != reverbs.end(),
         "track send, global depth, and signed reverb phase should all be preserved");

  const auto harmonyBytes = sequenceFixture({{
      0x87, 0, 55,                      // source pan from A Crisp Morning
      0x9b, 0, 3,  12,  0,    127, 40,  // delayed octave harmony with left intrinsic pan
      0x9a, 0, 1,                        // harmony on
      60,   2, 4,  100,
      0x9b, 0, 1,  0xf4, 0,   127, 100,  // configure the next clone without changing the active copy
      0x87, 0, 64,                       // center source pan leaves the clone at its intrinsic pan
      62,   2, 4,  100, 0x9a, 0,   0,    // harmony off
      64,   2, 4,  100, 0x80,
  }});
  const ByteReader harmonyReader(SourceId{93}, harmonyBytes);
  const auto harmonyLayout = readTriAcePs1SequenceLayout(harmonyReader, 0);
  expect(harmonyLayout.has_value(), "TriAcePS1 harmony fixture should have a valid layout");
  const PerformanceSequence harmony =
      SequenceVm(LoopPolicy::PlayOnce).render(parseTriAcePs1Sequence(harmonyReader, AssetId{93}, *harmonyLayout));
  expect(harmony.tracks.size() == 2, "harmony should render on one companion performance track");
  const auto sourceNotes = eventsOfType<NotePerformanceEvent>(harmony.tracks[0]);
  const auto harmonyNotes = eventsOfType<NotePerformanceEvent>(harmony.tracks[1]);
  expect(sourceNotes.size() == 3 && harmonyNotes.size() == 2 && harmonyNotes[0]->key == 72.0 &&
             harmonyNotes[0]->header.tick == 3 && harmonyNotes[1]->key == 74.0 && harmonyNotes[1]->header.tick == 5,
         "the companion should retain its enabled parameter snapshot until it stops");
  const auto sourcePans = eventsOfType<ChannelPanPerformanceEvent>(harmony.tracks[0]);
  const auto harmonyPans = eventsOfType<ChannelPanPerformanceEvent>(harmony.tracks[1]);
  expect(sourcePans.size() == 3 && harmonyPans.size() == 2 &&
             std::abs(harmonyPans[0]->position - 35.0 / 127.0) < 0.000001 && harmonyPans[0]->header.tick == 3 &&
             std::abs(harmonyPans[1]->position - 40.0 / 127.0) < 0.000001 && harmonyPans[1]->header.tick == 5,
         "the companion should combine its intrinsic pan with each delayed source-pan change");

  const auto loopingBytes = sequenceFixture({
      {
          0x8d,
          60,
          1,
          1,
          100,
          0x80,
      },
      {
          0x8e,
          0,  // 256 hardware plays form a practical song loop
          0x80,
      },
  });
  const ByteReader loopingReader(SourceId{92}, loopingBytes);
  const auto loopingLayout = readTriAcePs1SequenceLayout(loopingReader, 0);
  expect(loopingLayout.has_value(), "TriAcePS1 loop fixture should have a valid layout");
  const SequenceProgram loopingProgram = parseTriAcePs1Sequence(loopingReader, AssetId{92}, *loopingLayout);
  const PerformanceSequence looped = SequenceVm(SequenceVmOptions{
                                                    .loopPolicy = LoopPolicy::PlayOnce,
                                                    .sequenceLoops = 1,
                                                })
                                         .render(loopingProgram);
  expect(looped.diagnostics.empty(), "TriAcePS1 practical loop should render without diagnostics");
  expect(eventsOfType<NotePerformanceEvent>(looped.tracks.front()).size() == 2,
         "a zero repeat count should follow the requested sequence loop policy");
}

void triAcePs1ExtractorAndModuleBuildSelfContainedCollection() {
  const auto fixture = scannerFixture();
  expect(readTriAcePs1BankLayout(ByteReader(SourceId{92}, fixture), 0x1000).has_value(),
         "TriAcePS1 bank fixture should have a valid layout");
  Session session;
  session.registerExtractor(triAcePs1Extractor());
  session.registerFormat(triAcePs1Module());
  session.addSource(
      SourceFile{
          .name = "TriAcePS1 fixture.ram",
          .knownFormat = std::string(source_formats::kPlayStationRam),
      },
      fixture);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1,
         "SLZ stream with an omitted alignment tail should produce one TriAcePS1 collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.sequence.has_value() && collection.members.soundBanks.size() == 1,
         "the extracted sequence and RAM bank should remain in one collection (banks=" +
             std::to_string(collection.members.soundBanks.size()) + ")");
  const auto* bank = snapshot.asset<SoundBankAsset>(collection.members.soundBanks.front());
  expect(bank != nullptr && bank->instruments.size() == 1 && bank->localSamples.samples.size() == 1,
         "TriAcePS1 bank should retain its local SPU sample pool");
  const Instrument& instrument = bank->instruments.front();
  expect(instrument.identity == triAcePs1InstrumentIdentity(3, 0x12) && instrument.regions.size() == 1,
         "instrument identity should preserve the driver's full bank/program word");
  expect(std::abs(instrument.regions.front().unityKey - 59.216912152) < 0.000001,
         "instrument pitch should use the audited fractional driver-table root");
}

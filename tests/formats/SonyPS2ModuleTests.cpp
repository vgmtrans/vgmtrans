/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionBinding.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/extractors/PsfExtractor.h"
#include "value/formats/SonyPS2/SonyPS2.h"
#include "value/session/Session.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::sony_ps2;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(double actual, double expected) {
  return std::abs(actual - expected) < 0.000001;
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

void text(std::vector<u8>& bytes, size_t offset, std::string_view value) {
  std::ranges::copy(value, bytes.begin() + offset);
}

std::vector<u8> sqFixture(bool includeSong = true, bool repeatSong = false, u8 songVolumeReduction = 0) {
  const std::vector<u8> plain{
      0,    0xb0, 0,    0,     // bank 0
      0,    0xc0, 0,           // program 0
      0,    0xb0, 0,    1,     // pending bank only; no change until another program change
      0,    0xb0, 1,    64,    // program-scaled pitch modulation
      0,    0xb0, 2,    64,    // program-scaled amplitude modulation
      0,    0xb0, 7,    64,    // driver's linear channel volume
      0,    0xb0, 10,   64,    // additive channel pan, neutral at center
      0,    0xb0, 11,   96,    // driver's linear expression
      0,    0xb0, 5,    25,    // portamento time: 500 ms
      0,    0xb0, 84,   48,    // portamento source key
      0,    0xb0, 65,   1,     // any nonzero value enables portamento
      0,    0xb0, 99,   0x10,  // mark callback NRPN
      0,    0xb0, 98,   1,     // 14-bit mark callback
      0,    0xb0, 6,    0x12,  // mark data MSB
      0,    0xb0, 38,   0x34,  // mark data LSB
      0,    0xb0, 6,    1,     // inert after the driver consumes the NRPN selector
      0,    0xb0, 38,   2,
      0,    0xa0, 60,   70,    // ordinary (uncompressed) polyphonic pressure
      0,    0x90, 60,   0xe4,  // velocity 100 and omitted next zero delta
      0xe0, 0x7f, 0x7f,        // maximum positive pitch wheel
      10,   0x80, 60,          // note off
      0,    0xe0, 0,    64,    // center pitch wheel
      0,    0x90, 84,   100,   // note in the upper bend-range split
      0,    0xe0, 0x7f, 0x7f,  // maximum positive pitch wheel
      0,    0xe0, 0,    0,     // maximum negative pitch wheel
      10,   0x80, 84,          // note off
      0,    0xff, 0x2f, 0,     // end
  };
  const std::vector<u8> compressed{
      0,  0xa0, 0x0c,  // table 0, reconstructed velocity 103
      10, 0x80, 60,    // note off
      0,  0xb0, 10,   127,                    // pan changed at the end of this section
      0,  0xff, 0x51, 3, 0x0f, 0x42, 0x40,  // one-second tempo
      0,  0xff, 0x2f, 0,
  };
  constexpr u32 midiOffset = 0x30;
  constexpr u32 tableBytes = 8;
  const u32 firstBlock = 16 + tableBytes;
  const u32 secondBlock = firstBlock + 6 + static_cast<u32>(plain.size());
  const u32 midiSize = secondBlock + 12 + static_cast<u32>(compressed.size());
  std::vector<u8> bytes(midiOffset + midiSize, 0xff);
  text(bytes, 0, "IECSsreV");
  le32(bytes, 8, 16);
  bytes[14] = 2;
  text(bytes, 16, "IECSuqeS");
  le32(bytes, 24, 0x20);
  le32(bytes, 0x24, midiOffset);
  text(bytes, midiOffset, "IECSidiM");
  le32(bytes, midiOffset + 8, midiSize);
  le32(bytes, midiOffset + 12, 1);
  le32(bytes, midiOffset + 16, firstBlock);
  le32(bytes, midiOffset + 20, secondBlock);
  le32(bytes, midiOffset + firstBlock, 6);
  le16(bytes, midiOffset + firstBlock + 4, 480);
  std::ranges::copy(plain, bytes.begin() + midiOffset + firstBlock + 6);
  le32(bytes, midiOffset + secondBlock, 12);
  le16(bytes, midiOffset + secondBlock + 4, 480);
  le16(bytes, midiOffset + secondBlock + 6, 1);
  le16(bytes, midiOffset + secondBlock + 8, 2);
  bytes[midiOffset + secondBlock + 10] = 0x90;
  bytes[midiOffset + secondBlock + 11] = 60;
  std::ranges::copy(compressed, bytes.begin() + midiOffset + secondBlock + 12);
  if (!includeSong) {
    le32(bytes, 0x1c, static_cast<u32>(bytes.size()));
    return bytes;
  }
  const u32 songOffset = static_cast<u32>(bytes.size());
  bytes.resize(bytes.size() + 32, 0);
  text(bytes, songOffset, "IECSgnoS");
  le32(bytes, songOffset + 8, 32);
  le32(bytes, songOffset + 12, 0);
  le32(bytes, songOffset + 16, 20);
  u32 songCursor = songOffset + 20;
  if (songVolumeReduction != 0) {
    bytes[songCursor++] = 0xa0;
    bytes[songCursor++] = 3;
    bytes[songCursor++] = songVolumeReduction;
  }
  bytes[songCursor++] = 0xa0;
  bytes[songCursor++] = 0;
  bytes[songCursor++] = 1;
  if (repeatSong) {
    bytes[songCursor++] = 0xa0;
    bytes[songCursor++] = 0x11;
    bytes[songCursor++] = 1;
    bytes[songCursor++] = 0xa1;
    bytes[songCursor++] = 0;
    bytes[songCursor++] = 0;
  }
  bytes[songCursor++] = 0xa0;
  bytes[songCursor++] = 0x7f;
  bytes[songCursor] = 0x7f;
  le32(bytes, 0x20, songOffset);
  le32(bytes, 0x1c, static_cast<u32>(bytes.size()));
  return bytes;
}

u32 addSparseChunk(std::vector<u8>& bytes, std::string_view tag, u32 entryBytes) {
  const u32 offset = static_cast<u32>(bytes.size());
  bytes.resize(bytes.size() + 20 + entryBytes, 0);
  text(bytes, offset, tag);
  le32(bytes, offset + 8, 20 + entryBytes);
  le32(bytes, offset + 12, 0);
  le32(bytes, offset + 16, 20);
  return offset;
}

std::vector<u8> hdFixture() {
  std::vector<u8> bytes(0x40, 0xff);
  text(bytes, 0, "IECSsreV");
  le32(bytes, 8, 16);
  bytes[14] = 2;
  text(bytes, 16, "IECSdaeH");
  le32(bytes, 24, 0x40);
  le32(bytes, 0x20, 0x30);

  const u32 vagi = addSparseChunk(bytes, "IECSigaV", 8);
  le32(bytes, vagi + 20, 0);
  le16(bytes, vagi + 24, 24000);
  bytes[vagi + 26] = 1;

  const u32 smpl = addSparseChunk(bytes, "IECSlpmS", 42);
  le16(bytes, smpl + 20, 0);
  bytes[smpl + 22] = 1;
  bytes[smpl + 24] = 127;
  bytes[smpl + 27] = 16;
  bytes[smpl + 28] = 64;
  bytes[smpl + 30] = 16;
  bytes[smpl + 31] = 64;
  bytes[smpl + 31 + 0] = 64;
  bytes[smpl + 20 + 11] = 60;
  bytes[smpl + 20 + 12] = 64;
  bytes[smpl + 20 + 16] = 127;
  le16(bytes, smpl + 20 + 18, 0x8f7f);
  le16(bytes, smpl + 20 + 20, 0x1fcf);
  bytes[smpl + 20 + 22] = 12;
  bytes[smpl + 20 + 23] = 60;
  le16(bytes, smpl + 20 + 32, 25);
  le16(bytes, smpl + 20 + 36, 40);
  bytes[smpl + 20 + 40] = 0x11;
  bytes[smpl + 20 + 41] = 0x0c;

  const u32 sset = addSparseChunk(bytes, "IECStesS", 6);
  bytes[sset + 20] = 4;
  bytes[sset + 21] = 10;
  bytes[sset + 22] = 100;
  bytes[sset + 23] = 1;
  le16(bytes, sset + 24, 0);

  const u32 prog = addSparseChunk(bytes, "IECSgorP", 36 + 40);
  const u32 program = prog + 20;
  le32(bytes, program, 36);
  bytes[program + 4] = 2;
  bytes[program + 5] = 20;
  bytes[program + 6] = 127;
  bytes[program + 8] = 2;
  bytes[program + 9] = 64;
  bytes[program + 14] = 6;
  bytes[program + 15] = 3;
  le16(bytes, program + 20, 500);
  le16(bytes, program + 22, 1000);
  le16(bytes, program + 24, 0);
  le16(bytes, program + 26, 0);
  le16(bytes, program + 28, 128);
  le16(bytes, program + 30, static_cast<u16>(-256));
  bytes[program + 32] = 64;
  bytes[program + 33] = static_cast<u8>(-64);
  bytes[program + 34] = 0;
  bytes[program + 35] = static_cast<u8>(-64);
  const u32 split = program + 36;
  le16(bytes, split, 0);
  bytes[split + 2] = 60;
  bytes[split + 3] = 60;
  bytes[split + 4] = 60;
  le16(bytes, split + 6, 128);
  le16(bytes, split + 8, 384);
  bytes[split + 16] = 127;
  const u32 upperSplit = split + 20;
  le16(bytes, upperSplit, 0);
  bytes[upperSplit + 2] = 84;
  bytes[upperSplit + 3] = 84;
  bytes[upperSplit + 4] = 84;
  le16(bytes, upperSplit + 6, 768);
  le16(bytes, upperSplit + 8, 768);
  bytes[upperSplit + 16] = 127;

  const u32 setb = addSparseChunk(bytes, "IECSbteS", 66);
  const u32 set = setb + 20;
  le32(bytes, set, 0);
  le32(bytes, set + 4, 28);
  const u32 timbre = setb + 28;
  le32(bytes, timbre, 8);
  bytes[timbre + 4] = 50;
  bytes[timbre + 5] = 61;
  bytes[timbre + 6] = 61;
  const u32 note = timbre + 8;
  le16(bytes, note, 0);
  bytes[note + 2] = 2;
  bytes[note + 3] = 128;
  bytes[note + 4] = 64;
  bytes[note + 5] = static_cast<u8>(-1);
  bytes[note + 6] = 64;
  bytes[note + 13] = 0x0c;
  le16(bytes, note + 14, 0x8f7f);
  le16(bytes, note + 16, 0x1fcf);
  bytes[note + 18] = 0x80;
  le16(bytes, note + 24, 250);
  le16(bytes, note + 28, 64);
  le16(bytes, note + 30, static_cast<u16>(-64));
  le16(bytes, note + 34, 10);
  bytes[note + 49] = 0x01;

  le32(bytes, 0x1c, static_cast<u32>(bytes.size()));
  le32(bytes, 0x24, prog);
  le32(bytes, 0x28, sset);
  le32(bytes, 0x2c, smpl);
  le32(bytes, 0x30, vagi);
  le32(bytes, 0x34, setb);
  return bytes;
}

std::vector<u8> bdFixture() {
  std::vector<u8> bytes(0x30, 0x55);
  bytes[0] = bytes[0x10] = bytes[0x20] = 0x11;
  bytes[1] = 4;
  bytes[0x11] = 0;
  bytes[0x21] = 3;
  return bytes;
}

template <class Event>
size_t countEvents(const PerformanceSequence& performance) {
  size_t result = 0;
  for (const auto& track : performance.tracks) {
    result += std::ranges::count_if(track.events,
                                    [](const PerformanceEvent& event) { return std::holds_alternative<Event>(event); });
  }
  return result;
}

template <class Event, class Predicate>
const Event* findEvent(const PerformanceSequence& performance, Predicate predicate) {
  for (const auto& track : performance.tracks) {
    for (const auto& event : track.events) {
      if (const auto* typed = std::get_if<Event>(&event); typed != nullptr && predicate(*typed)) {
        return typed;
      }
    }
  }
  return nullptr;
}

template <class Event>
const Event* findEvent(const PerformanceSequence& performance) {
  return findEvent<Event>(performance, [](const Event&) { return true; });
}

const Collection* firstCollection(const SessionSnapshot& snapshot) {
  const auto found = std::ranges::find_if(snapshot.collections(), [](const Collection& collection) {
    return collection.members.sequence && !collection.members.soundBanks.empty() &&
           !collection.members.samplePools.empty();
  });
  return found == snapshot.collections().end() ? nullptr : &*found;
}

SourceFile archiveMember(std::string name) {
  SourceFile source{.name = name, .path = "/fixture/music.psf2"};
  source.attributes.emplace("container-format", "PSF2");
  source.attributes.emplace("container-member", std::move(name));
  return source;
}

SessionSnapshot scanFixture(std::vector<u8> sq) {
  Session session;
  session.registerFormat(module());
  session.addSource(archiveMember("music.sq"), std::move(sq));
  session.addSource(archiveMember("music.hd"), hdFixture());
  session.addSource(archiveMember("music.bd"), bdFixture());
  session.scanPendingSources();
  return session.snapshot();
}

void syntheticFeatures() {
  const auto snapshot = scanFixture(sqFixture(false));
  expect(std::ranges::none_of(snapshot.diagnostics(),
                              [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; }),
         "synthetic SonyPS2 scan should not report errors");
  expect(snapshot.collections().size() == 2, "both standalone MIDI blocks should publish a resolved collection");
  size_t allSequenceNotes = 0;
  for (const auto& candidate : snapshot.collections()) {
    const auto candidateBinding = bindCollection(snapshot, candidate.id);
    expect(candidateBinding.collection.has_value(), "every synthetic SonyPS2 collection should bind");
    const auto candidateRender = renderCollection(*candidateBinding.collection, SequenceRenderOptions{});
    expect(candidateRender.performance.has_value(), "every synthetic SonyPS2 sequence should render");
    allSequenceNotes += countEvents<NotePerformanceEvent>(*candidateRender.performance);
  }
  expect(allSequenceNotes == 3,
         "plain and compressed MIDI should render all three notes; got " + std::to_string(allSequenceNotes));
  const Collection* collection = firstCollection(snapshot);
  expect(collection != nullptr, "same-stem SQ/HD/BD should resolve into a collection");
  const auto bound = bindCollection(snapshot, collection->id);
  expect(bound.collection.has_value(), "SonyPS2 collection should bind");
  const auto& bank = bound.collection->soundBanks().front();
  expect(bank.instruments.size() == 2 && bank.localSamples.samples.empty(),
         "HD should retain both Prog and Setb instruments with external samples");
  expect(bank.instruments[0].regions.size() == 156,
         "sample-set limits and both split layers should retain their mapped velocity zones; got " +
             std::to_string(bank.instruments[0].regions.size()));
  const auto& region = bank.instruments[0].regions.front();
  expect(region.sample.valid() && near(region.unityKey, 69.0),
         "Vagi rate and all three tuning layers should determine unity key");
  expect(region.modulation.vibrato && region.modulation.tremolo,
         "program pitch and amplitude LFOs should survive as physical modulation");
  expect(region.modulation.vibrato->depthMode == ModulationDepthMode::Controller &&
             near(region.modulation.vibrato->maxDepthCents, 200.0) &&
             region.modulation.tremolo->depthMode == ModulationDepthMode::Fixed,
         "controller-only vibrato and fixed tremolo should retain their distinct SonyPS2 depth flows");
  expect(bank.instruments[0].pitchBendRangeCents == 600,
         "the instrument channel should accommodate its largest split bend range");
  expect(bank.instruments[0].reverb == 1.0 && bank.instruments[1].reverb == 1.0,
         "SPU2 wet-left/wet-right routing bits should retain reverb capability");
  expect(bound.collection->samplePools().front()->pool.samples.front().loop.enabled,
         "PSX ADPCM loop flags should survive HD/BD binding");
  expect(bank.instruments[1].identity && bank.instruments[1].identity->domain == kSetbInstrumentDomain &&
             bank.instruments[1].regions.size() == 117 && near(bank.instruments[1].regions.front().unityKey, 73.5),
         "Setb note-addressed timbres should retain velocity curves, tuning, and their distinct identity domain");
  expect(!bank.instruments[1].regions.front().modulation.vibrato,
         "a Setb note must not invent a default waveform when its custom LFO table is unavailable");

  const auto rendered = renderCollection(*bound.collection, SequenceRenderOptions{});
  expect(rendered.performance && countEvents<NotePerformanceEvent>(*rendered.performance) == 2,
         "compact zero-delta note-on/off should render both plain MIDI notes");
  const auto* note = findEvent<NotePerformanceEvent>(*rendered.performance);
  expect(note != nullptr && near(note->linearVelocity, 100.0 / 128.0),
         "note velocity should retain the driver's linear voice gain");
  const auto hasPhysicalBend = [&](double semitones) {
    return findEvent<PitchBendPerformanceEvent>(*rendered.performance, [&](const auto& event) {
             return near(event.semitones, semitones) && !event.normalizedWheelPosition;
           }) != nullptr;
  };
  expect(hasPhysicalBend(383.0 / 128.0) && hasPhysicalBend(767.0 / 128.0) && hasPhysicalBend(-6.0),
         "pitch wheel should switch between each active split's signed, driver-quantized range");
  const auto* vibrato = findEvent<ModulationPerformanceEvent>(*rendered.performance, [](const auto& event) {
    return event.target == ModulationPerformanceTarget::VibratoDepth;
  });
  expect(vibrato != nullptr && near(vibrato->amount, 64.0 / 127.0) && vibrato->pitchDepthSemitones &&
             near(*vibrato->pitchDepthSemitones, 1.0),
         "CC1 should retain both its raw controller amount and modhsyn's 0..128-scaled physical depth");
  const auto* tremolo = findEvent<ModulationPerformanceEvent>(*rendered.performance, [](const auto& event) {
    return event.target == ModulationPerformanceTarget::TremoloDepth;
  });
  expect(tremolo != nullptr && near(tremolo->amount, 64.0 / 127.0) && tremolo->volumeDepthLinearGain &&
             near(*tremolo->volumeDepthLinearGain, 0.25),
         "CC2 should retain both its raw controller amount and modhsyn's 0..128-scaled physical depth");
  const auto* level = findEvent<LevelPerformanceEvent>(
      *rendered.performance, [](const auto& event) { return near(event.linearGain, 64.0 / 127.0); });
  expect(level != nullptr, "CC7 should retain the driver's linear channel-volume gain");
  const auto* expression = findEvent<ExpressionPerformanceEvent>(
      *rendered.performance, [](const auto& event) { return near(event.linearGain, 96.0 / 127.0); });
  expect(expression != nullptr, "CC11 should retain the driver's linear expression gain");
  const auto* pan = findEvent<ChannelPanPerformanceEvent>(*rendered.performance);
  expect(pan != nullptr && near(pan->position, 0.5) && pan->voicePanLaw == PanLaw::ConstantMaximum,
         "CC10 should remain an additive constant-maximum voice-pan controller");
  const MidiSequence midi = renderMidiSequence(*rendered.performance);
  const auto midiNote = std::ranges::find_if(midi.tracks.front().events, [](const MidiEvent& event) {
    return std::holds_alternative<NoteDuration>(event.payload);
  });
  expect(midiNote != midi.tracks.front().events.end() && std::get<NoteDuration>(midiNote->payload).velocity == 112,
         "Sony velocity 100 should lower to an equivalent MIDI amplitude");
  const auto hasController = [&](MidiController controller, u16 value) {
    return std::ranges::any_of(midi.tracks.front().events, [&](const MidiEvent& event) {
      const auto* message = std::get_if<MidiChannelMessage>(&event.payload);
      return message != nullptr && message->kind == MidiChannelMessageKind::ControlChange &&
             message->parameter == static_cast<u8>(controller) && message->value == value;
    });
  };
  expect(hasController(MidiController::Pan, 64) && hasController(MidiController::Expression, 110),
         "SonyPS2 MIDI should lower CC10 directly without clipping the independent CC11 flow");
  const auto* portamentoSource = findEvent<PortamentoControlPerformanceEvent>(*rendered.performance);
  expect(portamentoSource != nullptr && near(portamentoSource->previousKey, 60.0),
         "note-off should provide the next SonyPS2 portamento source; CC84 is inert in the shipped driver");
  const auto* marker = findEvent<MarkerPerformanceEvent>(*rendered.performance);
  expect(marker != nullptr && marker->text == "SonyPS2 mark callback 2356" &&
             countEvents<MarkerPerformanceEvent>(*rendered.performance) == 1,
         "14-bit mark callbacks should combine CC6 and CC38 once, then consume the NRPN selector");

  const std::vector<u8> seBytes{
      0,   0xa2, 3,  61,   100,          // note on
      0,   0xb2, 3,  61,   0x20, 64, 0,  // two-byte amp-LFO depth used by the driver
      0,   0xb2, 3,  61,   0x0e, 10,     // raise pitch 100 cents over 10 ms
      100, 0,    10, 0xa2, 3,    61, 0, 0, 0xff,
  };
  const ByteReader seReader(SourceId{1}, seBytes);
  const auto se = parseSeSequence(seReader, AssetId{1},
                                  SeSequenceLayout{.offset = 0,
                                                   .dataOffset = 0,
                                                   .dataEnd = static_cast<u32>(seBytes.size()),
                                                   .ppqn = 1000,
                                                   .volume = 127,
                                                   .timeScale = 1000});
  expect(se.has_value(), "a note-bearing SeSeq should compile");
  const PerformanceSequence sePerformance = SequenceVm().render(*se);
  expect(countEvents<NotePerformanceEvent>(sePerformance) == 1,
         "SeSeq set/timbre note-on and note-off commands should render on their dedicated voice track");
  expect(sePerformance.tracks.size() == 1 && sePerformance.tracks.front().automations.size() == 1,
         "SeSeq pitch automation should remain isolated to its addressed set/timbre/note voice");
  const auto* pitch = std::get_if<PitchTransitionIntent>(&sePerformance.tracks.front().automations.front().intent);
  expect(pitch != nullptr && near(pitch->startKey, 61.0) && near(pitch->targetKey, 62.0) &&
             pitch->timing.timelineTicks == 10 &&
             near(std::get<FixedDurationPitchSlideTiming>(pitch->timing.physical).milliseconds, 10.0),
         "SeSeq Time-Pitch+ should preserve its relative cents and physical millisecond duration");
}

void trivialSongCollapsesToSelectedMidi() {
  const auto snapshot = scanFixture(sqFixture());
  expect(snapshot.collections().size() == 1 && snapshot.collections().front().name == "music MIDI 1",
         "a play-one-MIDI-and-end Song table should select its MIDI instead of publishing a duplicate sequence");
  const auto bound = bindCollection(snapshot, snapshot.collections().front().id);
  expect(bound.collection && bound.collection->samplePools().size() == 1,
         "a PSF2 BD member should bind even though its host path ends in .psf2");
  const auto rendered = renderCollection(*bound.collection, SequenceRenderOptions{});
  expect(rendered.performance && countEvents<NotePerformanceEvent>(*rendered.performance) == 1,
         "the MIDI selected by a trivial Song wrapper should remain playable");

  auto staleSizes = sqFixture();
  constexpr u32 staleBytes = 0x400;
  le32(staleSizes, 0x1c, static_cast<u32>(staleSizes.size()) + staleBytes);
  le32(staleSizes, 0x20, static_cast<u32>(staleSizes.size()) - 32 + staleBytes);
  le32(staleSizes, 0x38, static_cast<u32>(staleSizes.size()) - 32 - 0x30 + staleBytes);
  const auto staleSnapshot = scanFixture(std::move(staleSizes));
  expect(staleSnapshot.collections().size() == 2,
         "a shipped SQ with stale oversized length fields should retain its playable MIDI blocks");

  const auto statefulSnapshot = scanFixture(sqFixture(true, false, 30));
  expect(statefulSnapshot.collections().size() == 1 && statefulSnapshot.collections().front().name == "music Song 0",
         "a Song volume prefix should retain its wrapper while suppressing the private MIDI section");
  const auto statefulBinding = bindCollection(statefulSnapshot, statefulSnapshot.collections().front().id);
  expect(statefulBinding.collection.has_value(), "the stateful Song collection should bind");
  const auto statefulRender = renderCollection(*statefulBinding.collection, SequenceRenderOptions{});
  expect(statefulRender.performance && countEvents<NotePerformanceEvent>(*statefulRender.performance) == 1 &&
             countEvents<MasterLevelPerformanceEvent>(*statefulRender.performance) == 1,
         "a Song volume prefix should remain playable as song-wide gain");
  const auto* masterLevel = findEvent<MasterLevelPerformanceEvent>(*statefulRender.performance);
  expect(masterLevel != nullptr && near(masterLevel->linearGain, 98.0 / 128.0),
         "Song Volume - should use the driver's 0..128 linear master scale");

  auto unsupportedSong = sqFixture();
  const u32 songOffset = unsupportedSong[0x20] | (static_cast<u32>(unsupportedSong[0x21]) << 8) |
                         (static_cast<u32>(unsupportedSong[0x22]) << 16) |
                         (static_cast<u32>(unsupportedSong[0x23]) << 24);
  unsupportedSong[songOffset + 21] = 7;
  expect(scanFixture(std::move(unsupportedSong)).collections().size() == 2,
         "an unsupported Song table should remain annotated misc data without invalidating its MIDI blocks");
}

void repeatingSongRemainsPlaylist() {
  const auto snapshot = scanFixture(sqFixture(true, true));
  expect(snapshot.collections().size() == 1 && snapshot.collections().front().name == "music Song 0",
         "a repeating Song table should remain the sole public sequence instead of exposing its MIDI sections");
  const auto bound = bindCollection(snapshot, snapshot.collections().front().id);
  expect(bound.collection.has_value(), "the repeating Song collection should bind");
  const auto rendered = renderCollection(*bound.collection, SequenceRenderOptions{});
  expect(rendered.performance && countEvents<NotePerformanceEvent>(*rendered.performance) == 2,
         "a finite Song repeat should replay its selected MIDI section");
  const auto* panReset = findEvent<ChannelPanPerformanceEvent>(*rendered.performance, [](const auto& event) {
    return event.header.tick == 10 && near(event.position, 0.5);
  });
  const auto* tempoReset = findEvent<TempoPerformanceEvent>(*rendered.performance, [](const auto& event) {
    return event.header.tick == 10 && event.microsecondsPerQuarter == 500000;
  });
  expect(panReset != nullptr && tempoReset != nullptr,
         "each Song section should restore Song pan and tempo before the next MIDI block starts");
}

std::vector<u8> readFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  expect(static_cast<bool>(stream), "corpus file should be readable");
  const auto length = stream.tellg();
  stream.seekg(0);
  std::vector<u8> bytes(static_cast<size_t>(length));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

void realTriplet(const std::filesystem::path& sq, const std::filesystem::path& hd, const std::filesystem::path& bd) {
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = sq.filename().string(), .path = sq}, readFile(sq));
  session.addSource(SourceFile{.name = hd.filename().string(), .path = hd}, readFile(hd));
  session.addSource(SourceFile{.name = bd.filename().string(), .path = bd}, readFile(bd));
  session.scanPendingSources();
  const auto snapshot = session.snapshot();
  const Collection* collection = firstCollection(snapshot);
  if (collection == nullptr) {
    std::cerr << "real diagnostic: " << snapshot.assets().size() << " assets, " << snapshot.collections().size()
              << " collections\n";
    for (const auto& asset : snapshot.assets()) {
      std::visit([](const auto& value) { std::cerr << value.metadata.format << " " << value.metadata.name << '\n'; },
                 asset);
    }
    for (const auto& diagnostic : snapshot.diagnostics()) {
      std::cerr << diagnostic.message << '\n';
    }
  }
  expect(collection != nullptr, "real SQ/HD/BD triplet should resolve");
  const auto bound = bindCollection(snapshot, collection->id);
  if (!bound.collection) {
    for (const auto& diagnostic : bound.diagnostics) {
      std::cerr << "bind: " << diagnostic.message << '\n';
    }
  }
  expect(bound.collection.has_value(), "real SQ/HD/BD triplet should bind");
  const auto rendered = renderCollection(*bound.collection,
                                         SequenceRenderOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 0});
  expect(rendered.performance && countEvents<NotePerformanceEvent>(*rendered.performance) != 0,
         "real SonyPS2 sequence should render notes");
  std::cout << sq.filename().string() << ": " << bound.collection->soundBanks().front().instruments.size()
            << " instruments, " << countEvents<NotePerformanceEvent>(*rendered.performance) << " notes\n";
}

void realArchive(const std::filesystem::path& path) {
  Session session;
  session.registerExtractor(vgmtrans::formats::psf::psfExtractor());
  session.registerFormat(module());
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));
  session.scanPendingSources();
  const auto snapshot = session.snapshot();
  std::vector<const Collection*> sonyCollections;
  for (const auto& collection : snapshot.collections()) {
    if (!collection.members.sequence) {
      continue;
    }
    const auto* sequence = snapshot.asset<SequenceProgramAsset>(*collection.members.sequence);
    if (sequence != nullptr && sequence->metadata.format == kFormatName) {
      sonyCollections.push_back(&collection);
    }
  }
  if (sonyCollections.size() != 1) {
    std::cerr << "archive diagnostic: " << snapshot.sources().size() << " sources, " << snapshot.assets().size()
              << " assets, " << snapshot.collections().size() << " collections\n";
    for (const auto& source : snapshot.sources()) {
      std::cerr << "source: " << source.name << '\n';
    }
    for (const auto& diagnostic : snapshot.diagnostics()) {
      std::cerr << "scan: " << diagnostic.message << '\n';
    }
  }
  expect(sonyCollections.size() == 1, "a SonyPS2 archive should expose one selected Song or MIDI sequence");
  const bool g01Opening = path.filename() == "11 Tekken Tag Tournament - OPENING MOVIE.psf2";
  const bool striderFalloff = path.filename() == "19 Strider Hiryuu Bouei-ken ~ Fumikomu! (1 Stage BGM1).psf2";
  const bool streetFighterVelocity = path.filename() == "20 Street Fighter II - Stage Japan Ryu.psf2";
  if (g01Opening) {
    expect(sonyCollections.front()->name == "g01 MIDI 0",
           "the Namco X Capcom archive should expose only its selected g01 MIDI sequence");
  }
  const auto bound = bindCollection(snapshot, sonyCollections.front()->id);
  expect(bound.collection && bound.collection->soundBanks().size() == 1 && bound.collection->samplePools().size() == 1,
         "the g01 SQ, HD, and BD members should resolve into one complete collection");
  const auto rendered = renderCollection(*bound.collection,
                                         SequenceRenderOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 0});
  expect(rendered.performance && countEvents<NotePerformanceEvent>(*rendered.performance) != 0,
         "the resolved g01 collection should render notes");
  if (g01Opening) {
    const auto track2 = std::ranges::find(rendered.performance->tracks, 2u, &PerformanceTrack::sourceTrackNumber);
    expect(track2 != rendered.performance->tracks.end(), "the g01 performance should retain MIDI channel 2");
    const auto bendAt = [&](u64 tick, double semitones) {
      return std::ranges::any_of(track2->events, [&](const PerformanceEvent& event) {
        const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
        return bend != nullptr && bend->header.tick == tick && near(bend->semitones, semitones) &&
               !bend->normalizedWheelPosition;
      });
    };
    expect(bendAt(8760, 1535.0 / 128.0) && bendAt(9840, -12.0) && bendAt(77880, 3071.0 / 128.0),
           "g01 channel 2 should switch between its exact +/-12 and +/-24-semitone split ranges");
    const std::array<const SoundBankAsset*, 1> soundBanks{&bound.collection->soundBanks().front()};
    const MidiSequence midi =
        renderMidiSequence(*rendered.performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);
    const auto midiBendAt = [&](u64 tick, s16 value) {
      return std::ranges::any_of(midi.tracks[2].events, [&](const MidiEvent& event) {
        const auto* message = std::get_if<MidiChannelMessage>(&event.payload);
        return event.tick == tick && message != nullptr && message->kind == MidiChannelMessageKind::PitchBend &&
               message->value == value;
      });
    };
    expect(midiBendAt(8760, 4093) && midiBendAt(9840, -4096) && midiBendAt(77880, 8189),
           "g01 channel 2 should lower its physical bends through the 24-semitone MIDI channel range");
  }
  if (striderFalloff) {
    const std::array<const SoundBankAsset*, 1> soundBanks{&bound.collection->soundBanks().front()};
    const MidiSequence midi =
        renderMidiSequence(*rendered.performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);
    const auto expressionAt = [&](u64 tick, u16 value) {
      return std::ranges::any_of(midi.tracks[1].events, [&](const MidiEvent& event) {
        const auto* message = std::get_if<MidiChannelMessage>(&event.payload);
        return event.tick == tick && message != nullptr && message->kind == MidiChannelMessageKind::ControlChange &&
               message->parameter == static_cast<u8>(MidiController::Expression) && message->value == value;
      });
    };
    expect(expressionAt(29580, 126) && expressionAt(29775, 106),
           "Strider channel 1 should retain the unclipped expression fade after the note at SQ offset 0x15f3");
  }
  if (streetFighterVelocity) {
    const MidiSequence midi = renderMidiSequence(*rendered.performance);
    const auto noteAt = [&](size_t track, u64 tick, u8 key, u8 velocity) {
      return std::ranges::any_of(midi.tracks[track].events, [&](const MidiEvent& event) {
        const auto* note = std::get_if<NoteDuration>(&event.payload);
        return event.tick == tick && note != nullptr && note->key == key && note->velocity == velocity;
      });
    };
    expect(midi.tracks.size() > 9 && noteAt(7, 7920, 70, 117) && noteAt(7, 8040, 73, 110) && noteAt(7, 8160, 75, 87) &&
               noteAt(8, 8160, 75, 87) && noteAt(9, 8190, 75, 87),
           "Street Fighter channels 7-9 should retain the driver's linear velocity ratios");
  }
  const auto playback =
      session.preparePlayback(sonyCollections.front()->id,
                              PlaybackRequest{.sequence = {.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 0}});
  expect(playback.playable(), "the resolved g01 collection should produce playable MIDI and synth data");
  std::cout << path.filename().string() << ": one collection, "
            << countEvents<NotePerformanceEvent>(*rendered.performance) << " notes\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    syntheticFeatures();
    trivialSongCollapsesToSelectedMidi();
    repeatingSongRemainsPlaylist();
    if (argc == 2) {
      realArchive(argv[1]);
    } else if (argc == 4) {
      realTriplet(argv[1], argv[2], argv[3]);
    }
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "SonyPS2 test failure: " << exception.what() << '\n';
    return 1;
  }
}

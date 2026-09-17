/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "../MidiTestSupport.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::sculpt_soft_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void bytes(std::vector<u8>& data, u32 at, std::initializer_list<u8> values) {
  std::ranges::copy(values, data.begin() + at);
}
void word(std::vector<u8>& data, u32 at, u16 value) {
  data[at] = static_cast<u8>(value);
  data[at + 1] = static_cast<u8>(value >> 8);
}

template <class T>
std::vector<T> events(const PerformanceTrack& track) {
  std::vector<T> result;
  for (const auto& event : track.events) {
    if (const auto* value = std::get_if<T>(&event)) {
      result.push_back(*value);
    }
  }
  return result;
}

std::vector<u8> fixture(Revision revision = Revision::Standard) {
  std::vector<u8> data(kAramSize);
  bytes(data, 0x700, {0x8f, 0, 0x10, 0x8f, 0x30, 0x11, 0x8d, 3});
  bytes(data, 0x720, {0x8f, 0x20, 0xfa, 0x8f, 0x81, 0xf1});
  bytes(data, 0x730, {0xe4, 0xfd, 0x60, 0x84, 0x19, 0xc4, 0x19, 0x68, 5, 0x90, 0, 0xa8, 5, 0xc4, 0x19});
  bytes(data, 0x800,
        {0x8f, 0, 0x0b, 0x1c, 0x2b, 0x0b, 0x60, 0x96, 0, 0x20, 0xc4, 0x0a, 0xe4, 0x0b, 0x96, 1, 0x20, 0xc4, 0x0b});
  bytes(data, 0x840, {0x8d, 0x1a, 0x3f, 0, 8, 0xda, 0x6d, 0x8d, 0, 0xf7, 0x6d, 0xc4, 0x60, 0xc4, 0x61});
  bytes(data, 0x880, {0xf8, 0x62, 0xf4, 0x7f, 0x80, 0xa8, 5, 0xd4, 0x7f, 0x60, 0x94, 0x87, 0xfd, 0xcb, 0, 0xf8, 0x63});
  bytes(data, 0x900, {0xdd, 0x28, 0x0f, 0x1c, 0x5d, 0x1f, 0x40, 9});
  word(data, 0x94c, 0x880);
  word(data, 0x94e, 0xa00);
  word(data, 0x950, 0xa00);
  bytes(data, 0xa00, {0xf6, 0, 0x24, 0xc4, 0x2c, 0xf6, 0xf0, 0x24, 0xc4, 0x2d, 0xe8, 4, 0x80});
  bytes(data, 0xa20, {0x96, 0, 0x26, 0xd4, 0xab, 0xf4, 0xac, 0x96, 0x20, 0x26, 0xd4, 0xac});
  if (revision == Revision::Extended) {
    data[0xa0b] = 10;
    bytes(data, 0xa00 - 57, {0x60, 0x88, 0xa0, 0x5d, 0xdd, 0x88, 5, 0xfd, 0x7d, 0xad, 10});
    word(data, 0x956, 0xc00);
    word(data, 0x958, 0xc40);
    bytes(data, 0xc00, {0xe8, 0, 0xfd, 0xda, 0x3d, 0x8f, 1, 0xce, 0x3f, 0, 0xd, 0x2d, 0xfd, 0xf8, 0x65});
    bytes(data, 0xc40, {0x8f, 1, 0xcf, 0x5f, 0, 0xe});
  }
  word(data, 0x6d, 0x4000);
  for (u32 i = 0; i < 14; ++i) {
    word(data, 0x2000 + 2 * i, static_cast<u16>(0x2100 + 0x20 * i));
  }
  for (u32 i = 0; i < 240; ++i) {
    const u16 pitch = static_cast<u16>(4096 * std::exp2(i / 240.0));
    data[0x2400 + i] = static_cast<u8>(pitch);
    data[0x24f0 + i] = static_cast<u8>(pitch >> 8);
  }
  // Delta 0 = +one semitone; index 1 = zero for sustained notes.
  data[0x2600] = 20;
  bytes(data, 0x4000, {1, 0});
  word(data, 0x2200, 0x4100);
  word(data, 0x2120, 0x5000);
  bytes(data, 0x5000, {0, 127, 1, 0, 50, 0});
  word(data, 0x3004, 0x6003);
  word(data, 0x3006, 0x6003);
  bytes(data, 0x6000, {0, 0, 0x40, 3, 0, 0, 0, 0, 0, 0, 0, 0});
  bytes(data, 0x4100, {0xf2, 100, 0xf5, 0, 0xf7, 0xc0, 3, 4, 0xf0});
  return data;
}

PerformanceSequence render(const std::vector<u8>& data,
                           SequenceVmOptions options = {.loopPolicy = LoopPolicy::PlayOnce}) {
  const ByteReader reader(SourceId{70}, data);
  const auto layout = findLayout(reader);
  expect(layout.has_value(), "SculptSoftSnes fixture must recover relocated driver tables");
  const auto driver = readDriverData(reader, *layout);
  const auto program = decodeSequence(reader, *layout, driver, AssetId{0});
  auto result = SequenceVm(options).render(program);
  expect(result.diagnostics.empty(), result.diagnostics.empty() ? "" : result.diagnostics.front().message);
  return result;
}

void detectsTablesAndBuildsBank() {
  auto data = fixture();
  const auto layout = findLayout(ByteReader(SourceId{70}, data));
  expect(layout && layout->directory == 0x3000 && layout->tables == 0x2000 && layout->song == 0x4000 &&
             layout->pitchTable == 0x2400 && layout->deltaTable == 0x2600,
         "scanner must derive addresses from audited code, not game-specific constants");
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "Sculpt fixture.aram"}, data);
  session.scanPendingSources();
  const auto snapshot = session.snapshot();
  expect(snapshot.diagnostics().empty() && snapshot.collections().size() == 1 &&
             snapshot.collections().front().members.soundBanks.size() == 1,
         "scanner must build a complete sequence and BRR bank");
  data[0x94c] = 0;
  expect(!findLayout(ByteReader(SourceId{70}, data)), "an incompatible phrase command map must be rejected");
  data.resize(0xffff);
  expect(!findLayout(ByteReader(SourceId{70}, data)), "truncated ARAM must be rejected");
}

void noteTieRestAndZeroDurations() {
  auto data = fixture();
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 2, 0x00, 2, 0xf4, 2, 0xf3, 2, 0x20, 0, 0xf7, 0xe8, 3, 1, 0xf0});
  const auto performance = render(data);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 2 && notes[0].header.tick == 0 && notes[0].durationTicks == 6 && notes[1].header.tick == 8 &&
             notes[1].durationTicks == 1,
         "pitch-only notes and waits preserve one voice; rests cut it; zero notes do not attack");
  expect(std::abs(notes[0].key - 72) < 0.001 && std::abs(notes[1].key - 74) < 0.01,
         "absolute pitch must follow the driver table and octave calculation");
}

void tempoUpdatesPendingWaitsInPhysicalTime() {
  auto data = fixture();
  bytes(data, 0x4000, {2, 0, 1});
  word(data, 0x2202, 0x4200);
  // Higher channel changes the tempo halfway through channel zero's wait.
  bytes(data, 0x4200, {0xf4, 2, 0xfa, 128, 0, 1, 0xf4, 6, 0xf0});
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 6, 0xf0});
  const auto performance = render(data);
  const auto track = std::ranges::find(performance.tracks, 0u, &PerformanceTrack::sourceTrackNumber);
  const auto notes = events<NotePerformanceEvent>(*track);
  expect(notes.size() == 1 && notes.front().durationTicks == 9,
         "global half-speed tempo must retime an already pending wait using the shared accumulator");
}

void midiPreservesSampleTuningAndFinePitch() {
  auto data = fixture();
  word(data, 0x6000, 7);  // Sample tuning: +35 cents, before the hardware lookup.
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 2, 0x88, 2, 0xf0});
  const auto midi = renderMidiSequence(render(data));
  const auto& track = midi.tracks.front();
  const auto notes = midiNotes(track.events);
  expect(notes.size() == 1, "fine-pitch commands must bend the existing MIDI note");
  const auto ranges = midiPitchBendRanges(track.events);
  expect(!ranges.empty(), "sample tuning needs an explicit MIDI bend range");
  for (const auto [tick, index] : {std::pair{0u, 7u}, std::pair{2u, 9u}}) {
    double bend = 0;
    for (const auto& event : track.events) {
      if (const auto* message = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
          message && event.tick <= tick) {
        bend = message->value / 8192.0;
      }
    }
    const double expected = 72 + index / 20.0;
    const double actual = notes.front().key + bend * ranges.front().second / 100.0;
    expect(std::abs(actual - expected) < ranges.front().second / (8192.0 * 100),
           "MIDI note plus bend must preserve musical pitch and sample tuning within one wheel step");
  }
}

void semitoneAttacksKeepOneTuningBend() {
  for (const auto revision : {Revision::Standard, Revision::Extended}) {
    for (const u16 basePitch : {4096, 8192}) {
      for (const s16 tuning : {-7, 0, 7, 10}) {
        auto data = fixture(revision);
        for (u32 i = 0; i < 240; ++i) {
          const u16 pitch = static_cast<u16>(basePitch * std::exp2(i / 240.0));
          data[0x2400 + i] = static_cast<u8>(pitch);
          data[0x24f0 + i] = static_cast<u8>(pitch >> 8);
        }
        word(data, 0x6000, static_cast<u16>(tuning));
        bytes(data, 0x4100, {0xf2, 100});
        for (u16 i = 0; i < 36; ++i) {
          const u16 pitch = 480 + 20 * i;
          bytes(data, 0x4102 + 4 * i, {0xf7, static_cast<u8>(pitch), static_cast<u8>(pitch >> 8), 1});
        }
        data[0x4192] = 0xf0;
        const auto midi = renderMidiSequence(render(data));
        const auto& track = midi.tracks.front();
        const auto notes = midiNotes(track.events);
        expect(notes.size() == 36, "semitone runs must preserve every attack");
        const auto isBend = [](const auto& event) {
          return isMidiChannelMessage(event, MidiChannelMessageKind::PitchBend);
        };
        expect(std::ranges::count_if(track.events, isBend) == 1,
               "semitone changes must retain one tuning bend across octave boundaries in both revisions");
        const auto bend = std::ranges::find_if(track.events, isBend);
        const double cents = midiPitchBendRanges(track.events).front().second;
        const double offset = midiChannelMessage(*bend, MidiChannelMessageKind::PitchBend)->value * cents / 819200.0;
        for (size_t i = 0; i < notes.size(); ++i) {
          const double expected = (basePitch == 8192 ? 60 : 48) + i + tuning / 20.0;
          expect(notes[i].tick == i && std::abs(notes[i].key + offset - expected) < cents / 819200.0,
                 "table calibration and fractional sample tuning must survive semitone and octave changes");
        }
      }
    }
  }
}

void pitchCurvesPreserveFiveCentStepsAndAttackResets() {
  auto data = fixture();
  bytes(data, 0x5000, {4, 127, 1, 0, 50, 0});
  word(data, 0x2160, 0x5100);
  bytes(data, 0x5100, {0, 255, 0, 1, 0, 3, 2, 0, 0xb0, 4, 0xb1, 4, 0xb2, 4});
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 3, 0xf7, 0xd4, 3, 3, 0xf0});
  const auto performance = render(data);
  const auto bends = events<PitchBendPerformanceEvent>(performance.tracks.front());
  expect(bends.size() == 6, "each authored pitch-curve step and its attack reset must survive");
  for (size_t i = 0; i < bends.size(); ++i) {
    expect(bends[i].header.tick == i && std::abs(bends[i].semitones - (i % 3) / 20.0) < 1e-12,
           "five-cent pitch steps must remain exact and reset when the next note restarts the curve");
  }
}

void rawPitchRetainsDspResolution() {
  auto data = fixture(Revision::Extended);
  data[0x5000] = 0x40;
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0, 0x10, 2, 0xf8, 1, 0x10, 2, 0xf8, 1, 0x50, 2, 0xf0});
  const auto performance = render(data);
  const auto bends = events<PitchBendPerformanceEvent>(performance.tracks.front());
  expect(bends.size() == 2 && bends.back().header.tick == 2 &&
             std::abs(bends.back().semitones - 12 * std::log2(4097.0 / 4096)) < 1e-12,
         "raw pitch must retain sub-five-cent DSP changes and mask unused register bits");
}

void musicalPitchPreservesOctaveLimitsAndWrapping() {
  for (const auto revision : {Revision::Standard, Revision::Extended}) {
    for (const auto [pitch, key] : {std::pair{u16{1199}, 83.95}, std::pair{u16{1200}, 72.0},
                                    std::pair{u16{0xffff}, revision == Revision::Extended ? 23.95 : 35.95}}) {
      auto data = fixture(revision);
      bytes(data, 0x4100, {0xf2, 100, 0xf7, static_cast<u8>(pitch), static_cast<u8>(pitch >> 8), 1, 0xf0});
      const auto performance = render(data);
      const auto& track = performance.tracks.front();
      const double actual =
          events<NotePerformanceEvent>(track).front().key + events<PitchBendPerformanceEvent>(track).front().semitones;
      expect(std::abs(actual - key) < 1e-12,
             "musical pitch must preserve high-octave limits, signed low pitches and the standard driver's wrap");
    }
  }
}

void phrasesRestoreVolumeAndTranspose() {
  auto data = fixture();
  // Repeat a bounded phrase twice, transpose one semitone and halve volume.
  bytes(data, 0x4100, {0xf2, 100, 0xf6, 0, 0x42, 4, 0x42, 2, 20, 0, 0x80, 0, 0, 0xf7, 0xc0, 3, 2, 0xf0});
  bytes(data, 0x4200, {0xf7, 0xc0, 3, 2, 0xff});  // Exclusive end is deliberately invalid bytecode.
  const auto performance = render(data);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 3 && notes[0].header.tick == 0 && notes[1].header.tick == 2 && notes[2].header.tick == 4,
         "bounded phrase returns must happen before interpreting the byte at the exclusive end");
  expect(std::abs(notes[0].key - 73) < 0.01 && std::abs(notes[2].key - 72) < 0.01,
         "phrase transpose must be removed on return");
  const auto balance = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(balance.size() == 2 && balance[0].header.tick == 0 && balance[0].leftGain == 25.0 / 128 &&
             balance[1].header.tick == 4 && balance[1].leftGain == 50.0 / 128,
         "phrase volume must be scaled once, preserved on repeat, and restored on return");
}

void curvesFollowLoopReleaseAndInterpolation() {
  Curve sustain{.loopStart = 1, .loopEnd = 2, .releaseLead = 1, .speed = 1, .points = {0, 100, 80, 40, 0}};
  CurvePlayer player;
  player.start(sustain, 6);
  std::vector<u16> values{player.value};
  for (int i = 0; i < 9; ++i) {
    player.tick();
    values.push_back(player.value);
  }
  expect(values == std::vector<u16>({0, 0, 100, 80, 100, 80, 80, 40, 0, 0}),
         "software envelope must loop during its gate, jump to loop end, then consume release points");
  Curve ramp{.speed = 4, .interpolate = true, .points = {100, 0}};
  player.start(ramp, 0);
  values.clear();
  for (int i = 0; i < 9; ++i) {
    values.push_back(player.value);
    player.tick();
  }
  expect(values == std::vector<u16>({100, 100, 100, 100, 100, 75, 50, 25, 0}),
         "interpolation must use signed integer slopes and snap at countdown two");

  Curve extended{.loopStart = 5, .loopEnd = 4, .speed = 1, .pointCount = 3, .points = {10, 20, 30, 40, 50, 60}};
  player.start(extended, 1);
  values.clear();
  for (int i = 0; i < 5; ++i) {
    values.push_back(player.value);
    player.tick();
  }
  expect(values == std::vector<u16>({10, 10, 50, 60, 60}),
         "authored release indexes beyond the nominal point count must follow the SPC's address arithmetic");
}

void nestedPhrasesReplaceInstrumentsAndRespectLoopPolicy() {
  auto data = fixture();
  word(data, 0x2122, 0x5010);
  bytes(data, 0x5010, {0, 127, 2, 0, 50, 0});
  word(data, 0x3008, 0x6003);
  word(data, 0x300a, 0x6003);
  bytes(data, 0x4100, {0xf2, 100, 0xf6, 0, 0x42, 13, 0x42, 2, 20, 0, 128, 0, 0, 0xf7, 0xc0, 3, 1, 0xf0});
  bytes(data, 0x4200, {0xf6, 0, 0x43, 6, 0x43, 3, 20, 0, 128, 0, 2, 1, 0xff});
  bytes(data, 0x4300, {0xf5, 0, 0xf7, 0xc0, 3, 1, 0xff});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  expect(notes.size() == 7 && notes.front().key == 74 && notes.back().key == 72,
         "nested phrase transpose must accumulate and unwind independently");
  const auto instruments = events<InstrumentPerformanceEvent>(track);
  std::vector<std::pair<u64, u64>> samples;
  for (const auto& event : instruments) {
    samples.emplace_back(event.header.tick, std::get<InstrumentIdentity>(event.instrument).key);
  }
  expect(samples == std::vector<std::pair<u64, u64>>({{0, 2}, {1, 1}, {2, 2}, {4, 1}, {5, 2}}),
         "phrase replacement cursors must cycle across repeats; FF preserves the encoded patch");

  data[0x4107] = 0;  // The outer phrase loops forever.
  expect(events<NotePerformanceEvent>(render(data).tracks.front()).size() == 3 &&
             events<NotePerformanceEvent>(
                 render(data, {.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 2}).tracks.front())
                     .size() == 9,
         "an infinite phrase must honor the caller's sequence-loop policy");
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 1, 0xf9});
  expect(events<NotePerformanceEvent>(
             render(data, {.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 2}).tracks.front())
                 .size() == 3,
         "F9 track restarts must honor the same loop policy");
}

void repeatedAttacksOnlyEmitChangedVoiceState() {
  auto data = fixture();
  word(data, 0x2122, 0x5010);
  bytes(data, 0x5010, {0, 127, 2, 0, 25, 0});
  word(data, 0x3008, 0x6003);
  word(data, 0x300a, 0x6003);
  bytes(data, 0x4100, {0xf2, 100,  0xf7, 0xc0, 3,    2, 0xf7, 0xc0, 3, 2, 0xf3, 2, 0xf7, 0xc0, 3, 2, 0xf2, 50,
                       0xf7, 0xc0, 3,    2,    0xf5, 1, 0xf7, 0xc0, 3, 2, 0xf5, 0, 0xf7, 0xc0, 3, 2, 0xf0});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  expect(events<NotePerformanceEvent>(track).size() == 6, "unchanged voice state must still allow note retriggers");
  const auto instruments = events<InstrumentPerformanceEvent>(track);
  expect(instruments.size() == 3 && instruments[0].header.tick == 0 && instruments[1].header.tick == 10 &&
             instruments[2].header.tick == 12,
         "sample selections must persist across repeated attacks and rests, and emit changes in either direction");
  const auto balance = events<StereoBalancePerformanceEvent>(track);
  expect(balance.size() == 4 && balance[0].header.tick == 0 && balance[0].leftGain == 50.0 / 128 &&
             balance[1].header.tick == 8 && balance[1].leftGain == 25.0 / 128 && balance[2].header.tick == 10 &&
             balance[2].leftGain == 12.0 / 128 && balance[2].rightGain == 38.0 / 128 && balance[3].header.tick == 12 &&
             balance[3].leftGain == 25.0 / 128,
         "stereo output must suppress identical gains while retaining volume changes and integer pan rounding");

  data = fixture();
  bytes(data, 0x5000, {8, 127, 1, 0, 0, 0});
  word(data, 0x2180, 0x5100);
  bytes(data, 0x5100, {0, 255, 0, 1, 0, 1, 1, 1, 25});
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 2, 0xf7, 0xc0, 3, 2, 0xf0});
  const auto alternate = events<StereoBalancePerformanceEvent>(render(data).tracks.front());
  expect(alternate.size() == 2 && alternate[0].leftGain == 75.0 / 128 && alternate[1].header.tick == 2 &&
             alternate[1].leftGain == 25.0 / 128,
         "alternating pan must still swap the output gains when the underlying curve value is unchanged");
}

void softwareEnvelopesStayOnTheHardwareClock() {
  auto data = fixture();
  bytes(data, 0x5000, {13, 0, 1, 0, 0, 0});  // Gain, pitch and pan curves.
  word(data, 0x2140, 0x5100);
  bytes(data, 0x5100, {1, 2, 0, 1, 0, 3, 1, 0, 127, 32, 127});
  word(data, 0x2160, 0x5120);
  bytes(data, 0x5120, {1, 2, 0, 1, 0, 3, 2, 0, 0xb0, 4, 0xc4, 4, 0xb0, 4});
  word(data, 0x2180, 0x5140);
  bytes(data, 0x5140, {1, 2, 0, 1, 0, 3, 1, 1, 50, 25, 75});
  // Half-speed music still updates curves every 20 ms.
  bytes(data, 0x4100, {0xfa, 128, 0, 1, 0xf2, 100, 0xf7, 0xc0, 3, 6, 0xf0});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  const auto pitch = events<PitchBendPerformanceEvent>(track);
  expect(std::ranges::any_of(
             pitch, [](const auto& event) { return event.header.tick == 1 && std::abs(event.semitones - 1) < 0.005; }),
         "pitch curves must advance at hardware speed, independently of the music tempo");
  const auto pan = events<StereoBalancePerformanceEvent>(track);
  expect(std::ranges::any_of(pan,
                             [](const auto& event) {
                               return event.header.tick == 1 && event.leftGain == 75.0 / 128 &&
                                      event.rightGain == 25.0 / 128;
                             }),
         "alternating pan must exchange the driver's integer left/right gains");
  const auto gain = events<ExpressionPerformanceEvent>(track);
  expect(std::ranges::any_of(gain, [](const auto& event) { return event.header.tick == 2 && event.linearGain < 0.5; }),
         "GAIN curves must move the hardware envelope toward the next target");
}

void slowGainReleaseEventuallyReachesSilence() {
  auto data = fixture();
  bytes(data, 0x5000, {1, 0, 1, 0, 50, 0});
  word(data, 0x2140, 0x5100);
  bytes(data, 0x5100, {0, 0xff, 0, 1, 0, 2, 1, 0, 127, 0});
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 1, 0xf4, 0, 0xf0});
  const auto performance = render(data);
  const auto gain = events<ExpressionPerformanceEvent>(performance.tracks.front());
  expect(!gain.empty() && gain.back().linearGain == 0 && gain.back().header.tick < 257,
         "slow DSP GAIN rates must accumulate across frames instead of freezing above zero");
}

void sampleCurvesResolveTuningAndNoise() {
  auto data = fixture();
  bytes(data, 0x5000, {2, 127, 5, 0, 50, 0});
  word(data, 0x21aa, 0x5100);
  bytes(data, 0x5100, {0, 0xff, 0, 1, 0, 2, 1, 0, 1, 2});
  word(data, 0x6000, static_cast<u16>(-7));
  word(data, 0x3008, 0x6023);
  word(data, 0x300a, 0x6023);
  bytes(data, 0x6020, {0, 0, 0xdf, 3, 0, 0, 0, 0, 0, 0, 0, 0});
  const ByteReader reader(SourceId{70}, data);
  const auto driver = readDriverData(reader, *findLayout(reader));
  expect(driver.samples == std::set<u8>({1, 2}) && driver.sampleTuning[1] == -7 && driver.sampleFlags[2] == 0xdf,
         "a forward sample-curve reference must load all signed tuning words and noise flags");
  const auto performance = render(data);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 2 && notes[1].header.tick == 1 && notes[1].key == 72,
         "sample curves must switch instruments on the hardware clock, with unpitched noise");
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "noise.aram"}, data);
  session.scanPendingSources();
  const auto snapshot = session.snapshot();
  const auto* bank = snapshot.asset<SoundBankAsset>(snapshot.collections().front().members.soundBanks.front());
  expect(bank && bank->instruments.size() == 2 &&
             std::ranges::any_of(bank->localSamples.samples,
                                 [](const auto& sample) {
                                   return sample.codec == AudioCodec::SnesDspNoise && sample.codecParameter == 31;
                                 }),
         "noise SRCNs must export a synthesized SNES noise sample alongside BRR instruments");
}

void echoIsSharedAcrossVoices() {
  auto data = fixture();
  bytes(data, 0x4000, {2, 0, 1});
  word(data, 0x2202, 0x4200);
  word(data, 0x2122, 0x5010);
  bytes(data, 0x5010, {0x10, 127, 1, 0, 50, 0});
  word(data, 0x21c0, 0x5100);
  bytes(data, 0x5100, {3, 0xf0, 32, 0xc0, 127, 0, 0, 0, 0, 0, 0, 0});
  bytes(data, 0x4200, {0xf2, 100, 0xf5, 1, 0xf7, 0xc0, 3, 4, 0xf0});
  const auto performance = render(data);
  const auto track = std::ranges::find(performance.tracks, 0u, &PerformanceTrack::sourceTrackNumber);
  const auto echo = events<ReverbPerformanceEvent>(*track);
  expect(std::ranges::any_of(echo,
                             [](const auto& event) {
                               return event.voiceMask == 2 && event.leftGain == 75.0 / 128 &&
                                      event.rightGain == 32.0 / 128 && event.delayMilliseconds == 48 &&
                                      event.feedback == -0.5;
                             }),
         "a dry patch must preserve other voices' echo bits and the last global preset's unsigned gain clamp");
}

void playbackOwnsTablesAndReportsMalformedCommands() {
  SequenceProgram sequence;
  {
    auto data = fixture();
    const ByteReader reader(SourceId{70}, data);
    const auto layout = *findLayout(reader);
    sequence = decodeSequence(reader, layout, readDriverData(reader, layout), AssetId{0});
    std::ranges::fill(data, 0xff);
  }
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence);
  expect(performance.diagnostics.empty() && !events<NotePerformanceEvent>(performance.tracks.front()).empty(),
         "deferred playback must own its decoded tables and command operands");
  auto data = fixture();
  bytes(data, 0x4100, {0xff});
  const ByteReader reader(SourceId{70}, data);
  const auto layout = *findLayout(reader);
  std::vector<Diagnostic> diagnostics;
  const auto invalid =
      decodeSequence(reader, layout, readDriverData(reader, layout), AssetId{0}, nullptr, &diagnostics);
  const auto invalidPerformance = SequenceVm(LoopPolicy::PlayOnce).render(invalid);
  expect(!diagnostics.empty() || !invalidPerformance.diagnostics.empty(),
         "undefined commands must produce a diagnostic");
}

double gainAt(const PerformanceTrack& track, u64 tick) {
  double value = 1;
  for (const auto& event : events<ExpressionPerformanceEvent>(track)) {
    if (event.header.tick <= tick) {
      value = event.linearGain;
    }
  }
  return value;
}

void extendedRevisionRequiresMatchingCodeAndPreservesLowPitches() {
  auto data = fixture(Revision::Extended);
  expect(findLayout(ByteReader(SourceId{70}, data))->revision == Revision::Extended,
         "extended pitch conversion and command handlers must select the extended revision");
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0x10, 0xff, 2, 0xf0});  // -12 semitones in driver units.
  const auto notes = events<NotePerformanceEvent>(render(data).tracks.front());
  expect(notes.size() == 1 && notes.front().key == 12,
         "the extended six-octave bias must preserve pitches below the standard driver's range");
  data[0x5000] = 0x40;
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0, 0x10, 2, 0xf0});
  expect(events<NotePerformanceEvent>(render(data).tracks.front()).front().key == 72,
         "raw DSP pitch patches must bypass the extended pitch bias");
  data[0xc40] = 0;
  expect(!findLayout(ByteReader(SourceId{70}, data)), "a missing legato handler must reject the extended revision");
  data = fixture(Revision::Extended);
  data[0xa00 - 57] = 0;
  expect(!findLayout(ByteReader(SourceId{70}, data)),
         "a different pitch conversion must not be accepted by its table alone");
}

void extendedGateUsesBothBytesWithoutChangingNoteTiming() {
  auto data = fixture(Revision::Extended);
  bytes(data, 0x5000, {1, 0, 1, 0, 50, 0});
  word(data, 0x2140, 0x5100);
  bytes(data, 0x5100, {1, 2, 0, 1, 0, 4, 1, 0, 127, 127, 127, 0});
  bytes(data, 0x4100, {0xf2, 100,  0xfa, 0, 128, 1,    0xfb, 255,  45,  0xf7, 0xc0, 3,
                       0,    0xf7, 0xc0, 3, 2,   0xf4, 255,  0xf4, 255, 0xf4, 100,  0xf0});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  expect(notes.size() == 1 && notes.front().durationTicks == 612,
         "FB modifies the envelope gate, while zero-duration notes and following waits retain their normal timing");
  expect(gainAt(track, 500) > 0.9 && gainAt(track, 610) == 0,
         "the extended gate must scale and round both bytes separately: 300 at 1.5 becomes 578, not 450");

  // The pending gate is global: a wait leaves it available to the next channel.
  bytes(data, 0x4000, {2, 0, 1});
  word(data, 0x2202, 0x4200);
  bytes(data, 0x4200, {0xfb, 255, 45, 0xf4, 255, 0xf0});
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 2, 0xf4, 255, 0xf0});
  const auto shared = render(data);
  const auto voice = std::ranges::find(shared.tracks, 0u, &PerformanceTrack::sourceTrackNumber);
  expect(gainAt(*voice, 250) > 0.9, "the extended gate latch must be shared across channels");
}

void legatoAttackPreservesSamplePanEchoAndCurrentGain() {
  auto data = fixture(Revision::Extended);
  bytes(data, 0x5000, {0x19, 0, 1, 0, 0, 0});
  word(data, 0x2122, 0x5010);
  bytes(data, 0x5010, {9, 0, 2, 0, 0, 0});
  word(data, 0x3008, 0x6003);
  word(data, 0x300a, 0x6003);
  word(data, 0x2140, 0x5100);
  bytes(data, 0x5100, {0, 255, 0, 1, 0, 2, 1, 0, 127, 32});
  word(data, 0x2180, 0x5120);
  bytes(data, 0x5120, {0, 255, 0, 1, 0, 2, 1, 1, 20, 80});
  word(data, 0x21c0, 0x5140);
  bytes(data, 0x5140, {3, 64, 32, 0xc0, 127, 0, 0, 0, 0, 0, 0, 0});
  bytes(data, 0x4100, {0xf2, 100, 0xf7, 0xc0, 3, 3, 0xf5, 1, 0xf2, 50, 0xfc, 0xf7, 0xd4, 3, 4, 0xf7, 0xc0, 3, 1, 0xf0});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  expect(notes.size() == 2 && notes[0].durationTicks == 7 && notes[1].header.tick == 7,
         "FC suppresses the next attack's key-on and is consumed exactly once");
  const auto pan = events<StereoBalancePerformanceEvent>(track);
  expect(std::ranges::any_of(pan,
                             [](const auto& event) {
                               return event.header.tick == 3 && event.leftGain == 10.0 / 128 &&
                                      event.rightGain == 40.0 / 128;
                             }),
         "legato must retain pan-curve position and alternating-pan phase while updating volume");
  const auto echo = events<ReverbPerformanceEvent>(track);
  expect(std::ranges::none_of(echo, [](const auto& event) { return event.header.tick == 3; }),
         "legato must not replace global echo or clear the voice's echo bit");
  const auto instruments = events<InstrumentPerformanceEvent>(track);
  expect(instruments.size() == 2 && instruments.back().header.tick == 7 && gainAt(track, 3) < 0.5,
         "legato must retain the sample and current DSP envelope instead of resetting either");
}

std::vector<u8> earlyFixture() {
  auto data = fixture();
  bytes(data, 0xb00, {0x3f, 0x87, 0xb,  0x8d, 0x16, 0x3f, 0,    8,    0xf8, 0x5e, 0xd4, 0x7b,
                      0xdd, 0xd4, 0x7c, 0xf8, 0x5d, 0xe8, 0x0a, 0xd4, 0x61, 0x5f, 0,    0xc});
  bytes(data, 0xb80, {0x3f, 0x87, 0xb, 0x5d, 0x1f, 0, 0xd});
  word(data, 0xd08, 0xb00);
  bytes(data, 0xc00, {0x3f, 7, 0xc, 0x5d, 0x1f, 0x40, 0xd});
  word(data, 0xd42, 0xc40);
  bytes(data, 0xc40, {0x8d, 0x10, 0x3f, 0x80, 0xc, 0xe8, 6, 0xd4, 0x61, 0x5f, 0, 0xe});
  bytes(data, 0xc60, {0xf8, 0x5d, 0xe8, 0xa, 0xd4, 0x61, 0x5f, 0, 0xc});
  word(data, 0x940, 0xc60);
  word(data, 0x2280, 0x4100);
  word(data, 0x2260, 0x4200);
  word(data, 0x2200, 0x4300);
  bytes(data, 0x4100, {0x0a, 0, 4, 1, 8, 0, 0});
  bytes(data, 0x4200, {8, 0, 2, 0, 0});
  bytes(data, 0x4300, {0xc0, 3, 0xf2, 100, 0x21, 4, 0xf0});
  return data;
}

void earlyDetectionRequiresLayeredCommandTables() {
  auto data = earlyFixture();
  const auto layout = findLayout(ByteReader(SourceId{70}, data));
  expect(layout && layout->revision == Revision::Early, "early detection must validate the three-layer interpreter");
  const auto notes = events<NotePerformanceEvent>(render(data).tracks.front());
  expect(notes.size() == 1 && notes.front().durationTicks == 4 && notes.front().key == 72,
         "early song headers must index track table +18, then list +16 and pattern +10");
  word(data, 0xd42, 0xc60);
  expect(!findLayout(ByteReader(SourceId{70}, data)), "an incompatible early pattern command table must be rejected");
}

void earlyPatternsRetainStateAndUseBothInstruments() {
  auto data = earlyFixture();
  word(data, 0x2122, 0x5010);
  bytes(data, 0x5010, {0, 127, 2, 0, 50, 0});
  word(data, 0x3008, 0x6003);
  word(data, 0x300a, 0x6003);
  bytes(data, 0x4100, {0x0a, 0, 4, 2, 6, 20, 0, 8, 0, 8, 0, 0});
  bytes(data, 0x4200, {6, 40, 0, 8, 0, 0x0a, 1, 0x12, 20, 2, 0, 0});
  bytes(data, 0x4300, {0xc0, 3, 0xf1, 30, 0x21, 2, 0x61, 2, 0xf0});
  const ByteReader reader(SourceId{70}, data);
  const auto layout = *findLayout(reader);
  std::set<u8> programs;
  const auto sequence =
      decodeSequence(reader, layout, readDriverData(reader, layout), AssetId{0}, nullptr, nullptr, &programs);
  expect(programs == std::set<u8>({0, 1}), "both early patch selections must contribute to sound-bank discovery");
  std::ranges::fill(data, 0xff);
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence);
  expect(performance.diagnostics.empty(), "early playback must own all decoded data after the source is overwritten");
  const auto& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto instruments = events<InstrumentPerformanceEvent>(track);
  const auto balance = events<StereoBalancePerformanceEvent>(track);
  expect(notes.size() == 4 && notes[0].durationTicks == 4 && notes[2].header.tick == 8 && notes[3].header.tick == 12,
         "pattern returns must resume the list and track, with waits multiplied by the local divisor");
  expect(std::ranges::all_of(notes, [](const auto& note) { return note.key == 75; }),
         "track and pattern transpose must combine without accumulating on repeated calls");
  expect(instruments.size() == 4 && std::get<InstrumentIdentity>(instruments[0].instrument).key == 1 &&
             std::get<InstrumentIdentity>(instruments[1].instrument).key == 2,
         "bit 6 selects the secondary patch independently of bit 5's attack flag");
  expect(balance.size() == 2 && balance[0].leftGain == 25.0 / 128 && balance[1].leftGain == 40.0 / 128,
         "note volume is additive with list volume and persists through pattern calls");
}

void earlyFinePitchFixedPitchAndRests() {
  auto data = earlyFixture();
  word(data, 0x2262, 0x4240);
  bytes(data, 0x4100, {0x0a, 0, 4, 1, 6, 20, 0, 8, 0, 8, 1, 0});
  bytes(data, 0x4200, {8, 0, 6, 20, 0, 2, 0, 0});
  bytes(data, 0x4240, {0x0e, 0, 0, 2, 0, 0x0c, 2, 0, 0});
  bytes(data, 0x4300, {0xc0, 3, 0xf2, 100, 0x20, 0, 0x21, 2, 0x88, 2, 0xf4, 2, 0xf3, 1, 0xf0});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto bends = events<PitchBendPerformanceEvent>(track);
  expect(notes.size() == 3 && notes[0].key == 75 && notes[1].key == 72 && notes[2].key == 75 &&
             notes[0].durationTicks == 6 && notes[1].header.tick == 7 && notes[2].header.tick == 14,
         "zero notes update pitch without attacking; fixed mode suppresses deltas and both transposes; rests silence "
         "ties");
  expect(std::ranges::any_of(
             bends, [](const auto& bend) { return bend.header.tick == 2 && std::abs(bend.semitones - 0.1) < 1e-9; }) &&
             std::ranges::none_of(bends, [](const auto& bend) { return bend.header.tick == 9; }),
         "fine pitch bends the existing note in pitched mode and has no effect in fixed mode");
}

void earlyDivisorsAndLoopPolicy() {
  auto data = earlyFixture();
  bytes(data, 0x4000, {2, 0, 1});
  word(data, 0x2282, 0x4140);
  bytes(data, 0x4100, {0x0a, 0, 4, 2, 8, 0, 0});
  bytes(data, 0x4140, {0x0a, 1, 4, 3, 8, 0, 0});
  bytes(data, 0x4300, {0xc0, 3, 0xf1, 10, 0x21, 2, 0xf0});
  const auto once = render(data);
  for (const auto& track : once.tracks) {
    const auto notes = events<NotePerformanceEvent>(track);
    expect(notes.size() == 1 && notes.front().durationTicks == (track.sourceTrackNumber == 0 ? 4 : 6),
           "each early track has its own clock divisor");
  }
  data[0x4000] = 1;
  data[0x4106] = 2;
  const auto looped = render(data, {.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 1});
  const auto track = std::ranges::find(looped.tracks, 0u, &PerformanceTrack::sourceTrackNumber);
  const auto notes = events<NotePerformanceEvent>(*track);
  const auto balance = events<StereoBalancePerformanceEvent>(*track);
  expect(notes.size() == 2 && notes[1].header.tick == 4 && balance.back().leftGain == 10.0 / 128,
         "early restart respects loop policy and preserves musical state, including note volume");
  data = earlyFixture();
  bytes(data, 0x4100, {4, 2, 8, 0, 0});
  bytes(data, 0x4300, {0xc0, 3, 0xf4, 0, 0x21, 1, 0xf0});
  expect(events<NotePerformanceEvent>(render(data).tracks.front()).front().header.tick == 512,
         "zero wait duration wraps before multiplying by the local divisor");
}

void earlyEnvelopesKeepPhysicalTimeAndEncodedStride() {
  auto data = earlyFixture();
  bytes(data, 0x4100, {4, 3, 8, 0, 0});
  bytes(data, 0x5000, {0x0c, 127, 1, 0, 0, 0});
  word(data, 0x2160, 0x5100);
  bytes(data, 0x5100, {0, 255, 0, 1, 0, 2, 2, 0, 0xb0, 4, 0xc4, 4});
  word(data, 0x2180, 0x5140);
  // Some early pan curves use a stride larger than their two-byte value.
  bytes(data, 0x5140, {0, 255, 0, 1, 0, 2, 5, 0, 20, 0, 99, 99, 99, 80, 0});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  const auto bends = events<PitchBendPerformanceEvent>(track);
  const auto balance = events<StereoBalancePerformanceEvent>(track);
  expect(events<NotePerformanceEvent>(track).front().durationTicks == 12 &&
             std::ranges::any_of(bends, [](const auto& bend) { return bend.header.tick == 1 && bend.semitones == 1; }),
         "early envelopes advance every physical frame while the note counter runs at its local divisor");
  expect(balance.size() == 2 && balance[0].leftGain == 20.0 / 128 && balance[1].leftGain == 80.0 / 128 &&
             balance[1].header.tick == 1,
         "curve values use the encoded stride and the same physical frame clock");
}

void earlyUnsupportedAllocationAndInvalidPointersAreDiagnosed() {
  auto data = earlyFixture();
  bytes(data, 0x4100, {0x0a, 255, 0xf8, 4, 1, 8, 0, 0});
  ByteReader reader(SourceId{70}, data);
  const auto layout = *findLayout(reader);
  const auto result = SequenceVm().render(decodeSequence(reader, layout, readDriverData(reader, layout), AssetId{0}));
  expect(result.diagnostics.size() == 1 && result.diagnostics[0].message.find("voice allocation") != std::string::npos,
         "unsupported voice allocation must not silently flatten overlapping voices");
  auto shared = earlyFixture();
  bytes(shared, 0x4000, {2, 0, 0});
  const ByteReader sharedReader(SourceId{70}, shared);
  const auto sharedLayout = *findLayout(sharedReader);
  const auto sharedResult = SequenceVm().render(
      decodeSequence(sharedReader, sharedLayout, readDriverData(sharedReader, sharedLayout), AssetId{0}));
  expect(
      sharedResult.diagnostics.size() == 1 && sharedResult.diagnostics[0].message.find("Sharing") != std::string::npos,
      "two logical tracks must not silently claim independent playback on the same DSP voice");
  word(data, 0x2200, 0xffff);
  std::vector<Diagnostic> diagnostics;
  std::set<u8> programs;
  (void)decodeSequence(reader, layout, readDriverData(reader, layout), AssetId{0}, nullptr, &diagnostics, &programs);
  expect(!diagnostics.empty(), "unloaded early pattern pointers must stop decoding with a diagnostic");
}

std::vector<u8> lateFixture(bool indexed = false) {
  auto data = fixture(Revision::Extended);
  bytes(data, 0x700, {0x8f, 0xa0, 0xfa, 0x8f, 0xa0, 0xfb, 0x8f, 0x20, 0xfc, 0x8f, 0x87,
                      0xf1, 0x3f, 0,    0xc,  0x8f, 0,    0x25, 0x8f, 0x30, 0x26});
  data[0xfb] = 0xa0;
  word(data, 0x843, 0x7fd);
  word(data, 0x854, 0xc80);
  bytes(data, 0x980, {0xdd, 0x28, 0x1f, 0x1c, 0xfd, 0xf6, 0, 0xb, 0xc5, 0, 0, 0xf6, 1, 0xb, 0xc5, 0, 0});
  bytes(data, 0xc80, {0xe4, 0x88, 0x5d, 0x1c, 0xbc, 0xfd, 0xf7, 0x6d, 0xd4, 0x8c, 0xfc, 0xf7, 0x6d, 0xd4, 0xa0});
  if (indexed) {
    bytes(data, 0xc80,
          {0xeb, 0x88, 0xfc, 0xf7, 0x6d, 0x8d, 0x10, 0x3f, 0xfd, 7, 0xf8, 0x88, 0xd4, 0x8c, 0xdd, 0xd4, 0xa0});
  } else {
    word(data, 0x4001, 0x4100);
  }
  word(data, 0xb2c, 0xe80);
  word(data, 0xb2e, 0xf00);
  bytes(data, 0xeb2, {0xdd, 0x60, 0x95, 0x26, 6, 0xd5, 0x26, 6, 0xae, 0x95, 0x3a, 6, 0xd5, 0x3a, 6});
  bytes(data, 0xf00, {0x3f, 0, 0xe, 0xd5, 0x4e, 6, 0x3f, 0, 0xe, 0xd5, 0x62, 6, 0x2f});
  word(data, 0xb1e, 0xd00);
  bytes(data, 0xd00, {0x3f, 0, 0xe, 0xfd, 0x3f, 0, 0xe, 0x2d, 0xe8, 0});
  for (const auto& [op, duration] :
       std::array<std::pair<u8, u8>, 4>{{{0xf8, 32}, {0xfd, 64}, {0xfe, 128}, {0xff, 0}}}) {
    const u16 handler = static_cast<u16>(0xd20 + 4 * (op - 0xf8));
    word(data, 0xb00 + 2 * (op - 0xe0), handler);
    bytes(data, handler, {0xe8, duration, 0x2f, 0});
  }
  for (u32 i = 0; i < 240; ++i) {
    const u16 pitch = static_cast<u16>(8192 * std::exp2(i / 240.0));
    data[0x2400 + i] = static_cast<u8>(pitch);
    data[0x24f0 + i] = static_cast<u8>(pitch >> 8);
  }
  bytes(data, 0x4100, {0xf5, 0, 0xef, 69, 2, 0x49, 0xc1, 0xf0});
  return data;
}

void lateHeadersAndPackedSemitones() {
  for (const bool indexed : {false, true}) {
    auto data = lateFixture(indexed);
    const auto layout = findLayout(ByteReader(SourceId{70}, data));
    expect(layout && layout->revision == Revision::Late && layout->inlineTrackPointers != indexed,
           "late headers derive either direct pointers or table indexes from the track initializer");
    const auto performance = render(data);
    const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
    expect(notes.size() == 2 && notes[0].key == 60 && notes[1].key == 61 && notes[0].header.tick == 0 &&
               notes[1].header.tick == 2,
           "packed notes add signed semitone deltas and retain embedded durations");
    expect(events<PitchBendPerformanceEvent>(performance.tracks.front()).size() == 1,
           "ordinary late semitone notes must not emit per-note tuning corrections");
    data[0xb30] = 0;
    expect(!findLayout(ByteReader(SourceId{70}, data)), "an incompatible late command table must be rejected");
  }
}

void addHostQueue(std::vector<u8>& data, u8 capacity, bool portMirror) {
  bytes(data, 0x839, {0x2d, 0x3f, 0, 0x12, 0xae, 0x92, 7});
  bytes(data, 0x1000, {0xe4, 0xe8, 0xf0, 0x2f, 0x9c, 0xc4, 0xe8, 0xf8, 0xea, 0xf5, 0, 0x18, 0x28, 0xfe, 0xc4, 0x31});
  if (portMirror) {
    bytes(data, 0x1010,
          {0xc4, 0xf7, 0xf5, 0x40, 0x18, 0xc4, 0x32, 0xc4, 0xf4, 0xf5, 0x80, 0x18, 0xc4, 0xf5, 0xc4, 0x33});
  } else {
    bytes(data, 0x1010, {0xf5, 0x40, 0x18, 0xc4, 0x32, 0xf5, 0x80, 0x18, 0xc4, 0x33});
  }
  bytes(data, portMirror ? 0x1020 : 0x101a,
        {0x3d, 0xc8, capacity, 0xd0, 2, 0xcd, 0,    0xd8, 0xea, 0xe4, 0x31, 0x5d, 0x28,
         0xfe, 0xc8, 0x49,     0xb0, 7, 0xe4, 0x32, 0xeb, 0x33, 0x3f, 0,    0x11});
  bytes(data, 0x1100, {0x1f, 0x40, 0x11});
  word(data, 0x1146, 0x1180);
  bytes(data, 0x1180, {0x3f, 0x39, 8});
  word(data, 0x22a2, 0x4000);
  word(data, 0x23a2, 0x4040);  // Song 129 exercises the nine-bit table offset.
  bytes(data, 0x4040, {1, 1});
  word(data, 0x2202, 0x4180);
  bytes(data, 0x4180, {0xef, 71, 2, 0xf0});
}

void lateQueuedSongsOverrideStaleHeaders() {
  for (const bool indexed : {false, true}) {
    for (const bool portMirror : {false, true}) {
      for (const u8 capacity : {16, 20, 24, 32}) {
        auto data = lateFixture(indexed);
        addHostQueue(data, capacity, portMirror);
        if (!indexed) {
          word(data, 0x4041, 0x4180);
        }
        word(data, 0x6d, 0);
        expect(!findLayout(ByteReader(SourceId{70}, data)),
               "resident songs alone must not select an arbitrary song from an idle driver");
        // Two starts straddle the ring boundary. The newest request wins;
        // stale entries outside the pending span and the second argument do not.
        data[0xe8] = 2;
        data[0xea] = capacity - 1;
        data[0x1800 + capacity - 1] = 6;
        data[0x1840 + capacity - 1] = 1;
        bytes(data, 0x1800, {7, 6});
        bytes(data, 0x1840, {129, 255});
        data[0x1880] = 255;
        const auto layout = findLayout(ByteReader(SourceId{70}, data));
        expect(layout && layout->song == 0x4040,
               "pending starts must recover the requested song before the live header is initialized");
        word(data, 0x6d, 0x4000);
        const auto notes = events<NotePerformanceEvent>(render(data).tracks.front());
        expect(notes.size() == 1 && notes.front().key == 62,
               "a queued song must replace the previous live song, including its tracks");
        data[0xe8] = 0;
        expect(findLayout(ByteReader(SourceId{70}, data))->song == 0x4000,
               "an empty queue must leave the live song selected");
        data[0xe8] = 1;
        data[0xea] = 0;
        data[0x1800] = 4;
        expect(findLayout(ByteReader(SourceId{70}, data))->song == 0x4000,
               "a queued sound effect must not be interpreted as a song request");
      }
    }
  }
}

void lateInvalidQueuesAndUnloadedSongsAreRejected() {
  auto data = lateFixture();
  addHostQueue(data, 16, false);
  word(data, 0x6d, 0);
  data[0xe8] = 17;
  data[0x1800] = 6;
  data[0x1840] = 1;
  expect(!findLayout(ByteReader(SourceId{70}, data)), "an overflowing queue count must not select a song");
  data[0xe8] = 1;
  data[0xea] = 16;
  expect(!findLayout(ByteReader(SourceId{70}, data)), "an out-of-range queue head must not select a song");
  data[0xea] = 0;
  word(data, 0x100a, 0xfff8);
  expect(!findLayout(ByteReader(SourceId{70}, data)), "queue storage must fit within ARAM");
  word(data, 0x100a, 0x1800);
  word(data, 0x1181, 0x1234);
  expect(!findLayout(ByteReader(SourceId{70}, data)), "the host handler must call the detected song initializer");
  word(data, 0x1181, 0x839);
  word(data, 0x6d, 0x4000);
  word(data, 0x22a2, 0xffff);
  expect(!findLayout(ByteReader(SourceId{70}, data)),
         "an unloaded queued song must not silently export the previous song");
  word(data, 0x201a, 0);
  word(data, 2, 0x4000);
  expect(!findLayout(ByteReader(SourceId{70}, data)),
         "an absent song table must not read direct-page state as headers");
}

void lateFinePitchAndTempo() {
  auto data = lateFixture();
  bytes(data, 0x4100, {0xfa, 128, 0xef, 69, 2, 0xeb, 0x92, 3, 19, 0, 0xf4, 0xef, 70, 2, 0xf0});
  const auto performance = render(data);
  const auto& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto bends = events<PitchBendPerformanceEvent>(track);
  expect(notes.size() == 1 && notes.front().durationTicks == 11,
         "fine pitch and explicit ties retain the note while the shared tempo counter controls waits");
  expect(
      std::ranges::any_of(
          bends, [](const auto& bend) { return bend.header.tick == 3 && std::abs(bend.semitones - 0.05) < 1e-9; }) &&
          std::ranges::any_of(
              bends, [](const auto& bend) { return bend.header.tick == 7 && std::abs(bend.semitones - 1.0) < 1e-9; }),
      "fine streams retain five-cent steps, signed explicit deltas, and zero-time commands");
}

void lateCurvesReleaseAndKeepFractionalInterpolation() {
  Curve curve{.loopStart = 1, .loopEnd = 2, .speed = 2, .interpolate = true, .pointCount = 4, .points = {0, 3, 0, 0}};
  LateCurvePlayer player;
  player.start(curve, 0, false);
  player.tick(false);
  player.tick(false);
  player.tick(false);
  expect(player.accumulator == 96 && player.value == 1,
         "late byte envelopes interpolate in 10.6 fixed point before truncation");
  for (int i = 0; i < 20; ++i) {
    player.tick(false);
  }
  expect(player.active && player.held, "music envelope loops sustain until key-off");
  for (int i = 0; i < 20; ++i) {
    player.tick(true);
  }
  expect(!player.active && !player.held && player.value == 0,
         "key-off advances through the release tail and terminates the curve");
  player.start(curve, 48, false);
  expect(player.interval == 3 && player.step == 255, "lane speed uses a multiplier divided by 32");
  player.start(curve, 17, false);
  expect(player.interval == 1 && player.step == 253, "fractional lane speeds retain their phase step");
}

void lateVoiceHandoffsCloseThePreviousNote() {
  auto data = lateFixture();
  data[0x4000] = 2;
  word(data, 0x4003, 0x4200);
  bytes(data, 0x4100, {0xef, 69, 8, 0xf0});
  bytes(data, 0x4200, {0xc1, 0xe5, 8, 0, 255, 0xef, 71, 2, 0xf0});
  const auto performance = render(data);
  const auto first = events<NotePerformanceEvent>(performance.tracks[0]);
  const auto second = events<NotePerformanceEvent>(performance.tracks[1]);
  expect(first.size() == 1 && first[0].durationTicks == 2 && second.size() == 1 && second[0].header.tick == 2 &&
             second[0].key == 62,
         "a fixed DSP voice handed to another logical track must close its previous note at the handoff");
}

void phraseDiscoveryRetainsFineStreamInterpretations() {
  for (const bool conflict : {false, true}) {
    auto data = lateFixture();
    bytes(data, 0x4100, {0xf6, 0, 0x43, 3, 0x43, 1, 0, 0, 0, 1, 0,
                         0xf6, static_cast<u8>(conflict ? 1 : 0), 0x43, 4, 0x43, 1, 0, 0, 0, 1, 0, 0xf0});
    bytes(data, 0x4300, {0xeb, 0x11, 0, 0xf0});
    const ByteReader reader(SourceId{70}, data);
    const auto layout = *findLayout(reader);
    std::vector<Diagnostic> diagnostics;
    const auto program = decodeSequence(reader, layout, readDriverData(reader, layout), AssetId{0}, nullptr,
                                         &diagnostics);
    const auto& commands = program.tracks.front().commands;
    const auto at = [&](u32 address) -> const SourceCommand& {
      const auto found = std::ranges::find_if(commands, [&](const auto& command) {
        return command.address.value == address;
      });
      expect(found != commands.end(), "each bounded phrase should retain its discovered commands");
      return *found;
    };
    expect(at(0x4301).semantic == (conflict ? SequenceSemantic::Note : SequenceSemantic::Pitch),
           "revisited commands should retain their first fine-pitch interpretation");
    expect(at(0x4303).semantic == SequenceSemantic::End && at(0x4303).range.size == 1 &&
               at(0x4304).semantic == SequenceSemantic::Return && at(0x4304).range.size == 0,
           "only a phrase end without a real command should receive a synthetic return");
    expect(conflict ? diagnostics.size() == 1 && diagnostics.front().range.offset == 0x4301
                    : diagnostics.empty(),
           "compatible fine streams should reuse their decoded state; incompatible entry modes should be diagnosed");
  }
}

void latePhrasesAndAttackParameters() {
  auto data = lateFixture();
  bytes(data, 0x4100, {0xf2, 100, 0xf6, 0, 0x43, 4, 0x43, 2, 20, 0, 128, 0, 1, 255, 0xef, 71, 1, 0xf0});
  bytes(data, 0x4300, {0xef, 69, 2, 0x49});
  const auto phrase = render(data);
  const auto notes = events<NotePerformanceEvent>(phrase.tracks.front());
  expect(notes.size() == 5 && notes[0].key == 61 && notes[1].key == 62 && notes[2].key == 61 && notes[4].key == 62 &&
             notes[4].header.tick == 8,
         "late bounded phrases repeat embedded durations and restore pitch offsets at their address boundary");
  const auto balances = events<StereoBalancePerformanceEvent>(phrase.tracks.front());
  expect(balances.size() == 2 && balances[0].leftGain == 24.0 / 128 && balances[1].leftGain == 49.0 / 128,
         "phrase volume scaling is temporary and the attack multiplier keeps its byte truncation");

  bytes(data, 0x4100, {0xfb, 231, 0xef, 69, 2, 0xfb, 25, 0xe0, 0, 8, 0xf8, 0xef, 69, 2, 0xf0});
  const auto parameters = render(data);
  const auto shifted = events<NotePerformanceEvent>(parameters.tracks.front());
  const auto pans = events<StereoBalancePerformanceEvent>(parameters.tracks.front());
  expect(shifted.size() == 2 && shifted[0].key == 60 && shifted[1].key == 72 && shifted[1].header.tick == 34,
         "the raw DSP offset applies after musical pitch conversion and starts with the next note");
  expect(
      pans.size() == 2 && pans[0].leftGain == 31.0 / 128 && pans[1].leftGain == 94.0 / 128 && pans[1].header.tick == 34,
      "signed pan offsets are clamped and latched on attack rather than retiming the sounding voice");
}

void lateTrackEndPlaysTheReleaseTail() {
  auto data = lateFixture();
  data[0x5000] = 1;
  data[0x5001] = 0;
  word(data, 0x2140, 0x5100);
  bytes(data, 0x5100, {0, 1, 0, 1, 0, 3, 1, 0, 127, 127, 0});
  bytes(data, 0x4100, {0xef, 69, 2, 0xf0});
  const auto performance = render(data);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto gain = events<ExpressionPerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 1 && notes[0].durationTicks > 2 && !gain.empty() && gain.back().linearGain == 0,
         "track end keys off and lets a finite GAIN release settle before closing the note");
}

void lateAllocationLimitationsAreExplicit() {
  auto data = lateFixture();
  bytes(data, 0x4100, {0xe5, 0, 0, 3, 0xef, 69, 1, 0xf0});
  const ByteReader reader(SourceId{70}, data);
  const auto layout = *findLayout(reader);
  const auto result = SequenceVm().render(decodeSequence(reader, layout, readDriverData(reader, layout), AssetId{0}));
  expect(result.diagnostics.size() == 1 && result.diagnostics[0].message.find("voice allocation") != std::string::npos,
         "rotating allocation must be diagnosed instead of flattening independent physical voices");
  bytes(data, 0x4100, {0xe5, 16, 0, 255, 0xef, 69, 1, 0xe6, 0xef, 70, 1, 0xf0});
  const auto muted = render(data);
  const auto notes = events<NotePerformanceEvent>(muted.tracks.front());
  expect(notes.size() == 1 && notes[0].header.tick == 1 && notes[0].key == 61,
         "disabled allocation consumes note time and the default-voice command restores playback");
}

void lateTransposeVariantsAndCurveWidths() {
  for (const bool shared : {false, true}) {
    auto data = lateFixture();
    if (shared) {
      word(data, 0xf04, 0x626);
      word(data, 0xf0a, 0x63a);
    }
    bytes(data, 0x4100, {0xf7, 20, 0, 0xef, 69, 2, 0xf0});
    const auto layout = findLayout(ByteReader(SourceId{70}, data));
    expect(layout && layout->sharedTranspose == shared,
           "track pitch commands are distinguished by whether they address the phrase transposition field");
    const auto performance = render(data);
    expect(events<NotePerformanceEvent>(performance.tracks.front()).front().key == (shared ? 61 : 60),
           "shared transposition affects attacks while the separate late offset retains its driver quirk");
  }
  auto data = lateFixture();
  word(data, 0x2140, 0x5100);
  word(data, 0x2160, 0x5120);
  bytes(data, 0x5100, {0, 255, 0, 1, 0, 2, 17, 0, 127, 0});
  bytes(data, 0x5120, {0, 255, 0, 1, 0, 2, 1, 0, 0xb0, 4, 0xc4, 4});
  const ByteReader reader(SourceId{70}, data);
  const auto driver = readDriverData(reader, *findLayout(reader));
  expect(driver.curves[0][0]->points == std::vector<u16>({127, 0}) &&
             driver.curves[1][0]->points == std::vector<u16>({1200, 1220}),
         "late point widths come from the lane type and ignore header byte six");
}

}  // namespace

void runSculptSoftSnesModuleTests() {
  lateTrackEndPlaysTheReleaseTail();
  lateTransposeVariantsAndCurveWidths();
  phraseDiscoveryRetainsFineStreamInterpretations();
  latePhrasesAndAttackParameters();
  lateAllocationLimitationsAreExplicit();
  lateHeadersAndPackedSemitones();
  lateQueuedSongsOverrideStaleHeaders();
  lateInvalidQueuesAndUnloadedSongsAreRejected();
  lateFinePitchAndTempo();
  lateCurvesReleaseAndKeepFractionalInterpolation();
  lateVoiceHandoffsCloseThePreviousNote();
  earlyDetectionRequiresLayeredCommandTables();
  earlyPatternsRetainStateAndUseBothInstruments();
  earlyFinePitchFixedPitchAndRests();
  earlyDivisorsAndLoopPolicy();
  earlyEnvelopesKeepPhysicalTimeAndEncodedStride();
  earlyUnsupportedAllocationAndInvalidPointersAreDiagnosed();
  detectsTablesAndBuildsBank();
  noteTieRestAndZeroDurations();
  tempoUpdatesPendingWaitsInPhysicalTime();
  midiPreservesSampleTuningAndFinePitch();
  semitoneAttacksKeepOneTuningBend();
  pitchCurvesPreserveFiveCentStepsAndAttackResets();
  rawPitchRetainsDspResolution();
  musicalPitchPreservesOctaveLimitsAndWrapping();
  phrasesRestoreVolumeAndTranspose();
  curvesFollowLoopReleaseAndInterpolation();
  nestedPhrasesReplaceInstrumentsAndRespectLoopPolicy();
  repeatedAttacksOnlyEmitChangedVoiceState();
  softwareEnvelopesStayOnTheHardwareClock();
  slowGainReleaseEventuallyReachesSilence();
  sampleCurvesResolveTuningAndNoise();
  echoIsSharedAcrossVoices();
  playbackOwnsTablesAndReportsMalformedCommands();
  extendedRevisionRequiresMatchingCodeAndPreservesLowPitches();
  extendedGateUsesBothBytesWithoutChangingNoteTiming();
  legatoAttackPreservesSamplePanEchoAndCurrentGain();
}

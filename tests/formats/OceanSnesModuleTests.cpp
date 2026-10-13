/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../PerformanceTestSupport.h"
#include "../TestSupport.h"
#include "ValueFormatTestSupport.h"
#include "value/formats/OceanSnes/OceanSnesVoice.h"
#include "value/synth/SampleDecoder.h"
#include "value/validation/SequenceValidation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::ocean_snes;

namespace {

struct Fixture {
  std::vector<u8> bytes = std::vector<u8>(kAramSize);

  explicit Fixture(Version version = Version::Waterworld) {
    const bool jp2 = version == Version::JurassicPark2;
    const u16 dispatch = jp2 ? 0xa17 : 0xa1a;
    const u16 table = jp2 ? 0xa25 : 0xa28;
    write(dispatch, {0x80, 0xa8, 0xc0, 0x1c, 0xfd, 0xf6, 0, 0, 0x2d, 0xf6, 0, 0, 0x2d, 0x6f});
    word(dispatch + 6, table + 1);
    word(dispatch + 10, table);
    word(table + 4, jp2 ? 0xfdd : 0xfe0);
    write(jp2 ? 0xaa7 : 0xaaa,
          {0xfa, 0xe8, 0x30, 0xfa, 0xe9, 0x31, 0x8d, 0, 0xf7, 0x30, 0xc4, 0x34, 0xfc, 0xf7, 0x30, 0xc4, 0x35});
    write(0x640, {0x8f, 0xa2, 0xfa});
    if (jp2) {
      write(0xfdd, {0x3f, 4, 0xc, 0x10, 9, 0x48, 0xff, 0xbc, 0x5c, 0x48, 0xff, 0xbc, 0x2f, 1, 0x5c});
    } else {
      write(0xfe0, {0x3f, 7, 0xc, 0x1c, 0xc4, 0x5c, 0x3f, 0xba, 0x18});
    }
    word(0xe8, 0x2000);
    bytes[0x6c] = 96;
    bytes[0xd3] = 1;
    word(0x2000, 0x2020);
    word(0x2002, 0x3000);
    word(0x2004, 0x3100);
    bytes[0x2006] = 2;
    bytes[0x2008] = 255;
    std::fill(bytes.begin() + 0x2009, bytes.begin() + 0x2011, 255);
    orders(1);
    for (u32 track = 0; track < 8; ++track) {
      commands(track, {0xea, 0x99, 2, 0xbe, 2, 0xc7});
    }
    word(0x3000, 0x3002);
    write(0x3002, {1, 128, 0});
    write(0x3005, {0x80, 0xbd, 0, 31, 21, 127, 16, 0, 1, 255, 0, 0, 0, 64, 0, 0, 0, 0, 16, 0, 0, 0, 0});
    word(0x3100, 0x3102);
    word(0x3102, 0x1000);
    word(0x3104, 0);
    bytes[0x3106] = 3;  // END+LOOP, and deliberately no live DSP DIR entry.
    // Selected literal pitch constants from the resident driver.
    const u16 pitch = jp2 ? 0xe6a : 0xe6d;
    word(pitch, 0x42);
    word(pitch + 2, 0x46);
    word(pitch + 48, 0x10b);
    word(pitch + 50, 0x11b);
    word(pitch + 72, 0x217);
    word(pitch + 74, 0x237);
    word(pitch + 96, 0x42f);
  }

  void word(u32 at, u16 value) {
    bytes[at] = static_cast<u8>(value);
    bytes[at + 1] = value >> 8;
  }
  void write(u32 at, std::initializer_list<u8> values) { std::ranges::copy(values, bytes.begin() + at); }
  void commands(u32 track, std::initializer_list<u8> values) { write(0x2200 + track * 0x100, values); }
  void orders(u32 count) {
    for (u32 i = 0; i < count; ++i) {
      word(0x2020 + i * 2, 0x2080 + i * 16);
      for (u32 track = 0; track < 8; ++track) {
        word(0x2080 + i * 16 + track * 2, 0x2200 + track * 0x100);
      }
    }
    word(0x2020 + count * 2, 0xff01);  // The sentinel tests the high byte only.
  }
  ByteReader reader() const { return ByteReader(SourceId{50}, bytes); }
  SequenceProgram program(std::vector<Diagnostic>* diagnostics = nullptr) const {
    std::vector<Diagnostic> synthDiagnostics;
    const auto layout = findLayout(reader());
    expect(layout.has_value(), "synthetic Ocean driver must be recognized");
    auto data = std::make_shared<const DriverData>(readDriverData(reader(), *layout, synthDiagnostics));
    expect(synthDiagnostics.empty(), "synthetic instrument/sample structures must be valid");
    return decodeSequence(RetainedSource::copyOf(reader()), *layout, data, AssetId{50}, nullptr, diagnostics);
  }
};

void detectionValidatesRevisionAndSong() {
  for (const auto version : {Version::Waterworld, Version::JurassicPark2}) {
    Fixture f(version);
    const auto original = findLayout(f.reader());
    expect(original && original->version == version, "Ocean revisions must be distinguished by executable code");
    const u32 dispatch = version == Version::Waterworld ? 0xa1a : 0xa17;
    const u32 header = version == Version::Waterworld ? 0xaaa : 0xaa7;
    std::copy_n(f.bytes.begin() + dispatch, 14, f.bytes.begin() + 0x400);
    std::copy_n(f.bytes.begin() + header, 17, f.bytes.begin() + 0x450);
    const auto duplicated = findLayout(f.reader());
    expect(duplicated && duplicated->version == version,
           "copies of signatures in runtime RAM must not hide the resident driver at its known addresses");
    f.bytes[version == Version::Waterworld ? 0xa7c : 0xa79] = 0x73;
    expect(findLayout(f.reader()).has_value(), "mutable RNG bytes must not affect recognition");
    f.bytes[0x640] = 0;
    expect(!findLayout(f.reader()), "partial unrelated signatures must not identify Ocean");
  }
  Fixture f;
  f.word(0x2080, 0x100);
  expect(!findLayout(f.reader()), "a track pointing into runtime RAM must invalidate the song header");
  f = Fixture{};
  f.word(0x2002, 0xffff);
  expect(!findLayout(f.reader()), "truncated pointer tables must be rejected before instrument scanning");
  f = Fixture{};
  f.bytes.resize(0xffff);
  expect(!findLayout(f.reader()), "a truncated ARAM image must not be accepted");
}

void timingAndOrderIdentityArePreserved() {
  Fixture f;
  f.orders(3);  // Three orders reuse each pattern; this is not a pattern loop.
  const auto performance = SequenceVm().render(f.program());
  expect(performance.diagnostics.empty(), "ordinary Ocean playback must not produce diagnostics");
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 3, "reused pattern addresses must play once for each order");
  expect(notes[0]->header.tick == 2 && notes[0]->durationTicks == 6,
         "KON is delayed one base tick; explicit release follows the radix counter");
  const auto source = f.program().tracks.front().commands[notes[0]->header.sourceCommand.id.value];
  expect(source.range.offset == 0x2201, "delayed key-on must retain the note command's source association");
  expect(notes[1]->header.tick == 18 && notes[2]->header.tick == 34,
         "the next order must be primed at each 16-tick barrier");
  const auto repeated = SequenceVm({.sequenceLoops = 1}).render(f.program());
  expect(eventsOfType<NotePerformanceEvent>(repeated.tracks.front()).size() == 6,
         "Ocean order loops must honor the common VM repeat policy");
}

void prefixAndCachedDurationControlParsing() {
  Fixture f;
  f.commands(0, {0xea, 0xbf, 0x99, 0xc4, 0, 1, 0xbe, 2, 0xc7});
  const auto p = SequenceVm().render(f.program());
  const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
  expect(!notes.empty() && notes[0]->header.tick == 2 && notes[0]->durationTicks == 2,
         "BF must force C4 and its duration into the note's parse pass");
  const auto balances = eventsOfType<StereoBalancePerformanceEvent>(p.tracks.front());
  expect(!balances.empty() && balances.front()->leftGain == balances.front()->rightGain,
         "C4 must consume its operand without setting pan");

  f = Fixture{};
  for (u32 track = 0; track < 8; ++track) {
    f.commands(track, {0xea, 0x99, 0xca, 1, 1, 0xbe, 1, 0xc7});
  }
  const auto extended = SequenceVm().render(f.program());
  const auto longNotes = eventsOfType<NotePerformanceEvent>(extended.tracks.front());
  expect(longNotes.size() == 1 && longNotes[0]->durationTicks == 510,
         "CA01/01 must last 128 rows, not 257, minus the delayed KON tick");
}

void partialBarrierPrimesTheNextStream() {
  Fixture f;
  f.orders(2);
  // Track 0 reaches C7 at row 1, the other tracks at row 4.
  f.commands(0, {1, 0xc7});
  for (u32 t = 1; t < 8; ++t) {
    f.commands(t, {4, 0xc7});
  }
  f.word(0x2090, 0x2b00);
  f.write(0x2b00, {0xea, 0x99, 2, 0xbe, 1, 0xc7});
  const auto p = SequenceVm().render(f.program());
  const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
  expect(!notes.empty() && notes[0]->header.tick == 6,
         "partial C7 arrival must schedule the next note before the final track reaches the barrier");
}

void revisionVolumeAndSlidesUseDriverArithmetic() {
  for (const auto version : {Version::Waterworld, Version::JurassicPark2}) {
    Fixture f(version);
    const auto layout = *findLayout(f.reader());
    std::vector<Diagnostic> diagnostics;
    auto data = std::make_shared<const DriverData>(readDriverData(f.reader(), layout, diagnostics));
    const RuntimeConfig config{.source = RetainedSource::copyOf(f.reader()), .layout = layout, .data = data};
    Voice v{.region = &data->instruments[0]->regions[0],
            .instrumentVolume = 128,
            .eventVolume = 64,
            .trackVolume = 255,
            .regionVolume = 255};
    PerformanceTrackFixture output;
    v.tick(config, 0, 255, 96, 1, output.out);
    const auto balance = eventsOfType<StereoBalancePerformanceEvent>(output.track).back();
    expect(balance->leftGain == (version == Version::Waterworld ? 5.0 : 2.0) / 128,
           "canonical caps and JP2 multipliers must retain each integer truncation");
    v.eventVolume = 3;
    v.volumeStep = -8;
    v.tick(config, 0, 255, 96, 1, output.out);
    expect(v.eventVolume == 251, "negative event-volume underflow must wrap instead of being saturated");
    v.pan = 10 << 8;
    v.panTo(30, 4);
    for (int i = 0; i < 4; ++i) {
      v.tick(config, 0, 255, 96, 1, output.out);
    }
    expect(v.pan == 30 << 8 && v.panTicks == 0, "C1 uses the logical slot as its actual pan divisor");
  }
}

void samplesAreDiscoveredWithoutTheDspDirectory() {
  Fixture f;
  ScanIdAllocator ids;
  const auto result = module().scan({.source = {.name = "fixture.aram"}, .reader = f.reader(), .ids = ids});
  const SoundBankAsset* bank = nullptr;
  for (const auto& asset : result.assets) {
    if (const auto* value = std::get_if<SoundBankAsset>(&asset)) {
      bank = value;
    }
  }
  expect(bank && bank->instruments.size() == 1 && bank->localSamples.samples.size() == 1,
         "instrument-referenced samples must be found even with an empty DSP DIR");
  expect(bank->localSamples.samples[0].loop.enabled && bank->localSamples.samples[0].loop.length == 16,
         "sample header loop offsets must be converted from BRR bytes to PCM frames");
  const auto decoded = decodeSample(bank->localSamples.samples[0], f.bytes);
  expect(decoded && decoded->pcm.size() == 16, "the extracted BRR sample must decode through the shared codec");
  expectScanSharesPlaybackSource(module(), f.bytes);

  f.bytes[0x3106] = 1;
  f.word(0x3104, 0xffff);
  std::vector<Diagnostic> diagnostics;
  const auto data = readDriverData(f.reader(), *findLayout(f.reader()), diagnostics);
  expect(data.samples[0] && !data.samples[0]->loop.enabled && diagnostics.empty(),
         "a one-shot BRR sample must ignore its unused loop offset");
}

void capturedGainRampsContinueOncePerDriverTick() {
  for (const auto version : {Version::Waterworld, Version::JurassicPark2}) {
    Fixture f(version);
    f.word(0x6b, 0);
    f.word(0x6f, 0x2000);
    f.bytes[0x64] = 3;
    f.bytes[0x67] = 96;
    const auto p = SequenceVm().render(f.program());
    for (const auto& track : p.tracks) {
      const auto levels = eventsOfType<StereoBalancePerformanceEvent>(track);
      expect(levels.size() >= 3 && levels[0]->header.tick == 0 && levels[0]->leftGain == 3.0 / 128 &&
                 levels[1]->header.tick == 2 && levels[1]->leftGain == 7.0 / 128 && levels[2]->header.tick == 4 &&
                 levels[2]->leftGain == 11.0 / 128,
             "a captured fade-in must advance on base ticks, once for all eight voices");
    }

    // Mission Theme 1 is captured at zero gain, one tick before target 96.
    f.bytes[0x64] = 1;
    f.word(0x6f, 1);  // Final ticks snap to the target, independent of rounding.
    const auto unmuted = SequenceVm().render(f.program());
    expect(eventsOfType<StereoBalancePerformanceEvent>(unmuted.tracks.front())[0]->leftGain == 11.0 / 128,
           "a one-tick fade-in must restore music gain before the first delayed KON");

    f.word(0x6b, 0x60aa);
    f.word(0x6f, static_cast<u16>(-0x2055));
    f.bytes[0x64] = 3;
    f.bytes[0x67] = 0;
    const auto faded = SequenceVm().render(f.program());
    const auto levels = eventsOfType<StereoBalancePerformanceEvent>(faded.tracks.front());
    expect(levels.size() >= 3 && levels[0]->leftGain == 7.0 / 128 && levels[1]->leftGain == 3.0 / 128 &&
               levels[2]->leftGain == 0,
           "signed 8.8 fade-out steps must retain their fraction and reach an exact zero target");

    f.bytes[0x64] = 0;
    f.word(0x6b, 0x6000);
    f.bytes[0x63] = 1;
    f.bytes[0x66] = 0;
    const auto masterMuted = SequenceVm().render(f.program());
    expect(eventsOfType<StereoBalancePerformanceEvent>(masterMuted.tracks.front())[0]->leftGain == 0,
           "song initialization must retain a pending host master-gain ramp");
  }
}

void retriggersCutReleaseTailsButReleasesAndSlidesDoNot() {
  Fixture f;
  for (u32 track = 0; track < 8; ++track) {
    f.commands(track, {0xea, 0x99, 1, 0x99, 1, 0xbe, 1, 0x99, 1, 0xcf, 0x99, 1, 1, 0xbe, 1, 0xc7});
  }
  const auto p = SequenceVm().render(f.program());
  const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
  const auto cuts = eventsOfType<SilencePerformanceEvent>(p.tracks.front());
  expect(notes.size() == 3 && cuts.size() == 2 && cuts[0]->header.tick == 4 && cuts[1]->header.tick == 12,
         "only retriggers must cut the hardware voice, including a tail from an already released note");
  expect(notes[0]->header.tick == 2 && notes[0]->durationTicks == 2 && notes[1]->header.tick == 6 &&
             notes[2]->header.tick == 14,
         "KOFF must precede the next delayed KON by one base tick without changing ordinary note gates");
}

void invalidNotesCancelPendingRetriggers() {
  Fixture f;
  for (u32 track = 0; track < 8; ++track) {
    // Program 1 is invalid. It replaces a valid retrigger in the same frame,
    // before KON; the next valid note must still play normally.
    f.commands(track, {0xea, 0x99, 1, 0x99, 0, 0xeb, 0x99, 1, 0xea, 0x9a, 1, 0xbe, 1, 0xc7});
  }
  const auto p = SequenceVm().render(f.program());
  const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
  expect(!p.diagnostics.empty(), "invalid instruments must be diagnosed");
  expect(notes.size() == 2 && notes[0]->header.tick == 2 && notes[1]->header.tick == 10 && notes[1]->key == 49,
         "suppressing an invalid note must cancel a pending retrigger without preventing later valid notes");
}

void gainEnvelopesControlAutomaticRelease() {
  struct Case {
    u8 attack, decay, sustain, sustainTicks;
    u64 duration;
  };
  // At rate 31, attack + decay to ENVX < 64 takes 227 DSP samples: less than
  // one base tick. Zero rates freeze a stage; zero thresholds/timers hold.
  for (const auto test : {Case{31, 31, 64, 3, 6}, Case{31, 31, 127, 3, 6}, Case{0, 31, 64, 3, 398},
                          Case{31, 0, 64, 3, 398}, Case{31, 31, 0, 1, 398}, Case{31, 31, 64, 0, 398}}) {
    Fixture f;
    f.bytes[0x3005 + 3] = test.attack;
    f.bytes[0x3005 + 4] = test.decay;
    f.bytes[0x3005 + 5] = test.sustain;
    f.bytes[0x3005 + 7] = test.sustainTicks;
    for (u32 track = 0; track < 8; ++track) {
      f.commands(track, {0xea, 0x99, 100, 0xbe, 1, 0xc7});
    }
    const auto p = SequenceVm().render(f.program());
    const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
    expect(notes.size() == 1 && notes.front()->durationTicks == test.duration,
           "automatic gates must use physical GAIN timing and wait for explicit release when the envelope holds");
  }
}

void zeroPitchIsSilentAndRecoversOnTheNextNote() {
  Fixture f;
  f.commands(0, {0xea, 0x83, 1, 0x99, 1, 0xbe, 2, 0xc7});  // Fixture table entry 2 is zero.
  const auto p = SequenceVm().render(f.program());
  const auto expression = eventsOfType<ExpressionPerformanceEvent>(p.tracks.front());
  expect(expression.size() >= 2 && expression[0]->linearGain == 0 && expression[0]->header.tick == 2 &&
             expression[1]->linearGain == 1,
         "zero DSP pitch must silence playback and restore expression when pitch becomes nonzero");
}

void alternatingSpeedsFollowEachRevision() {
  for (const auto version : {Version::Waterworld, Version::JurassicPark2}) {
    Fixture f(version);
    f.commands(0, {0xd9, 0x23, 0xea, 0x99, 1, 0x99, 1, 0x99, 1, 0x99, 1, 0xbe, 8, 0xc7});
    const auto p = SequenceVm().render(f.program());
    const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
    expect(notes.size() >= 4 && notes[0]->header.tick == 2 && notes[1]->header.tick == 6 &&
               notes[2]->header.tick == (version == Version::Waterworld ? 10 : 12) && notes[3]->header.tick == 16,
           "D9 must alternate row speeds in the revision's actual nibble order");
  }
}

void portamentoKeepsFractionAndSnapsToItsTarget() {
  Fixture f;
  f.word(0x3102, 0xff00);
  f.word(0x3005 + 11, 0x200);  // Combined tuning wraps to 0x100 (1.0).
  const auto layout = *findLayout(f.reader());
  std::vector<Diagnostic> diagnostics;
  auto data = std::make_shared<const DriverData>(readDriverData(f.reader(), layout, diagnostics));
  const RuntimeConfig config{.source = RetainedSource::copyOf(f.reader()), .layout = layout, .data = data};
  Voice v{.region = &data->instruments[0]->regions[0]};
  v.pitch = 0x42;
  v.pitchFraction = 200;
  v.vibratoAmount = 3;
  v.bendUnit = 7;
  v.portamento(config, 0x82, 3, 0);
  PerformanceTrackFixture output;
  v.tick(config, 0, 255, 96, 1, output.out);
  expect(v.pitch == 0x44 && v.pitchFraction == 29,
         "portamento must keep the preceding glide's fractional pitch and truncate its new step");
  v.tick(config, 0, 255, 96, 1, output.out);
  v.tick(config, 0, 255, 96, 1, output.out);
  expect(v.pitch == 0x46 && v.pitchFraction == 0 && v.slideTicks == 0 && v.bendUnit == 7,
         "the final glide tick snaps to target without refreshing bend sensitivity");
  v.portamento(config, 0x81, 0, 0);
  v.tick(config, 0, 255, 96, 1, output.out);
  expect(v.pitch == 0x46, "zero-duration portamento must remain idle");
}

void nullInstrumentsAndConditionalOperandsPreserveTheStream() {
  Fixture f;
  // Program 1 is a null pointer, followed by the first actual record.
  std::copy_n(f.bytes.begin() + 0x3002, 26, f.bytes.begin() + 0x3020);
  std::fill(f.bytes.begin() + 0x3002, f.bytes.begin() + 0x3020, 0);
  f.word(0x3000, 0x3020);
  f.word(0x3002, 0);
  f.commands(0, {0xd8, 0xea, 0x99, 1, 0xeb, 0x9a, 1, 0xbe, 1, 0xc7});
  const auto p = SequenceVm().render(f.program());
  const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
  expect(notes.size() == 2 && notes[0]->key == 48 && notes[1]->key == 49,
         "D8 without a voice consumes no operand; null instruments keep the previous region and header");
}

void regionsUseFirstMatchAndRetainStateOnAMiss() {
  Fixture f;
  std::copy_n(f.bytes.begin() + 0x3002, 26, f.bytes.begin() + 0x3040);
  std::fill(f.bytes.begin() + 0x3002, f.bytes.begin() + 0x3040, 0);
  f.word(0x3000, 0x3040);
  f.word(0x3002, 0x3080);
  std::copy_n(f.bytes.begin() + 0x3040, 26, f.bytes.begin() + 0x3080);
  f.bytes[0x3081] = 64;
  f.bytes[0x3083] = 0xb0;  // Program 1 has no match for the second note.
  f.bytes[0x3040] = 2;
  f.bytes[0x3044] = 0x99;
  std::copy_n(f.bytes.begin() + 0x3043, 23, f.bytes.begin() + 0x305a);
  f.bytes[0x305a] = 0x90;
  f.bytes[0x305b] = 0xbd;
  f.bytes[0x305a + 13] = 0x81;  // Preserve pan AND surround on a matching new region.
  f.bytes[0x3043 + 22] = 200;
  f.commands(0, {0xea, 0x99, 1, 0xeb, 0x98, 1, 0xea, 0x9a, 1, 0xbe, 1, 0xc7});
  const auto p = SequenceVm().render(f.program());
  const auto selections = eventsOfType<InstrumentPerformanceEvent>(p.tracks.front());
  expect(selections.size() >= 3 && std::get<InstrumentIdentity>(selections[1]->instrument).key == 0,
         "a region miss retains the former instrument's sample while updating the header");

  ScanIdAllocator ids;
  const auto result = module().scan({.source = {.name = "regions.aram"}, .reader = f.reader(), .ids = ids});
  for (const auto& asset : result.assets) {
    if (const auto* bank = std::get_if<SoundBankAsset>(&asset)) {
      const auto& regions = bank->instruments.front().regions;
      expect(regions.size() == 2 && regions[0].keyRange.high == 48 && regions[1].keyRange.low == 49,
             "overlapping source regions must become nonoverlapping first-match zones");
    }
  }
}

void pitchArithmeticPreservesRevisionSpecificWrapping() {
  for (const auto version : {Version::Waterworld, Version::JurassicPark2}) {
    Fixture f(version);
    f.word(0x3102, 256);
    const auto layout = *findLayout(f.reader());
    f.word(layout.pitchTable + 254, 0x1234);
    std::vector<Diagnostic> diagnostics;
    const auto data = std::make_shared<const DriverData>(readDriverData(f.reader(), layout, diagnostics));
    const RuntimeConfig config{.source = RetainedSource::copyOf(f.reader()), .layout = layout, .data = data};
    Voice v{.region = &data->instruments[0]->regions[0], .instrumentTranspose = 255};
    v.pendingNote = 0x81;
    v.calculatePitch = true;
    PerformanceTrackFixture output;
    v.tick(config, 0, 255, 96, 1, output.out);
    expect(v.pitch == 0x42, "ordinary negative transposition must clamp to pitch-table entry zero");
    v.portamento(config, 0x81, 1, 0);
    v.tick(config, 0, 255, 96, 1, output.out);
    expect(v.pitch == (version == Version::Waterworld ? 0x42 : 0x1234),
           "JP2 portamento omits the negative-index clamp and wraps its table byte index");
    f.word(layout.pitchTable + 96, 0xffff);
    f.word(0x3102, 0xffff);
    const auto extreme = readDriverData(f.reader(), layout, diagnostics);
    const double unityKey = extreme.instruments[0]->regions[0].unityKey;
    expect(unityKey > -72.0 && unityKey < -71.99,
           "full-width pitch and tuning words must not overflow during instrument calibration");
  }
}

void retainedRegionsKeepAPlayableKeyAcrossOverlaps() {
  Fixture f;
  f.bytes[0x3002] = 2;
  std::copy_n(f.bytes.begin() + 0x3005, 23, f.bytes.begin() + 0x301c);
  f.bytes[0x3005] = 0x90;
  f.bytes[0x3006] = 0x99;
  f.bytes[0x301c] = 0x81;
  f.bytes[0x301d] = 0x90;
  f.bytes[0x301f] = 8;      // The second region has a different attack envelope.
  f.word(0xe6d + 8, 0x54);  // A nonzero pitch for the first note.
  for (u32 track = 0; track < 8; ++track) {
    f.commands(track, {0xea, 0x85, 1, 0x9a, 1, 0xbe, 1, 0xc7});
  }
  const auto p = SequenceVm().render(f.program());
  const auto notes = eventsOfType<NotePerformanceEvent>(p.tracks.front());
  expect(notes.size() == 2 && notes[0]->key == 28 && notes[1]->key == 28,
         "a region miss must retain a playable key; clamping to 0x90 would select the earlier region");
  const auto bends = eventsOfType<PitchBendPerformanceEvent>(p.tracks.front());
  expect(!bends.empty() && bends.back()->semitones > 12,
         "the retained region's key must not replace the newly requested hardware pitch");
}

void malformedCommandsAndInstrumentsAreBounded() {
  Fixture f;
  f.commands(0, {0xc9});
  auto program = f.program();
  program.behavior.commandLimit = 1000;
  const auto p = SequenceVm().render(program);
  expect(
      std::ranges::any_of(p.diagnostics, [](const Diagnostic& d) { return d.message.find("C9") != std::string::npos; }),
      "reserved memory clearing must be diagnosed, never followed as music");
  f = Fixture{};
  f.bytes[0x3002] = 0;
  std::vector<Diagnostic> diagnostics;
  const auto data = readDriverData(f.reader(), *findLayout(f.reader()), diagnostics);
  expect(!data.instruments[0] && !diagnostics.empty(), "zero region counts must not scan 256 unrelated RAM records");

  f = Fixture{};
  f.word(0x2080, 0xfffe);
  f.write(0xfffe, {0xcd, 0x99});
  diagnostics.clear();
  program = f.program(&diagnostics);
  expect(!diagnostics.empty(), "a truncated multi-byte command must be diagnosed");
  expect(validateSequenceProgram(program).empty(), "truncated commands must still produce structurally valid bytecode");
  static_cast<void>(SequenceVm().render(program));

  // Mutate source commands with a reproducible generator, including jumps,
  // prefixes and unsupported operations. The VM budget also bounds no-time loops.
  u32 random = 0x4f636561;
  for (u32 iteration = 0; iteration < 32; ++iteration) {
    f = Fixture{};
    for (u32 i = 0; i < 64; ++i) {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      f.bytes[0x2200 + i] = static_cast<u8>(random);
    }
    f.bytes[0x2240] = 0xc7;
    program = f.program();
    program.behavior.commandLimit = 2000;
    expect(validateSequenceProgram(program).empty(), "mutated command streams must retain valid IR structure");
    static_cast<void>(SequenceVm().render(program));
  }
}

void corpus(const std::filesystem::path& root, const std::filesystem::path& traces = {}) {
  u32 recognized = 0, rejected = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".spc") {
      continue;
    }
    std::ifstream input(entry.path(), std::ios::binary);
    const std::vector<u8> file((std::istreambuf_iterator<char>(input)), {});
    expect(file.size() >= 0x10100, "SPC corpus file was truncated");
    ByteReader reader(SourceId{100}, std::span<const u8>(file).subspan(0x100, kAramSize));
    const auto layout = findLayout(reader);
    const auto filename = entry.path().filename().string();
    const bool expected = filename.starts_with("wawo-") || filename.starts_with("afv") ||
                          filename.starts_with("flint") || filename.starts_with("jp2") || filename == "waterworld.spc";
    expect(layout.has_value() == expected, "corpus detection must match the independently identified driver families");
    if (!layout) {
      ++rejected;
      continue;
    }
    ++recognized;
    std::vector<Diagnostic> diagnostics;
    const auto data = std::make_shared<const DriverData>(readDriverData(reader, *layout, diagnostics));
    const auto program =
        decodeSequence(RetainedSource::copyOf(reader), *layout, data, AssetId{100}, nullptr, &diagnostics);
    expect(validateSequenceProgram(program).empty(), "corpus sequence structure must pass core validation");
    const auto performance = SequenceVm().render(program);
    u64 notes = 0;
    for (const auto& track : performance.tracks) {
      notes += eventsOfType<NotePerformanceEvent>(track).size();
    }
    expect(notes != 0 && performance.tracks.size() == 8, "recognized Ocean music must render eight tracks with notes");
    if (!traces.empty()) {
      std::filesystem::create_directories(traces);
      std::ofstream trace(traces / (entry.path().filename().string() + ".csv"));
      for (u32 t = 0; t < performance.tracks.size(); ++t) {
        for (const auto* note : eventsOfType<NotePerformanceEvent>(performance.tracks[t])) {
          trace << t << ',' << note->header.tick << ',' << note->key << ',' << note->durationTicks << '\n';
        }
      }
      std::ofstream controls(traces / (filename + ".controls.csv"));
      controls << std::setprecision(17);
      for (u32 t = 0; t < performance.tracks.size(); ++t) {
        for (const auto& event : performance.tracks[t].events) {
          const auto header = performanceEventHeader(event);
          const auto prefix = std::to_string(t) + ',' + std::to_string(header.tick) + ',';
          if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
            controls << prefix << "note," << note->key << '\n';
          } else if (const auto* pitch = std::get_if<PitchBendPerformanceEvent>(&event)) {
            controls << prefix << "pitch," << pitch->semitones << '\n';
          } else if (const auto* volume = std::get_if<StereoBalancePerformanceEvent>(&event)) {
            controls << prefix << "volume," << volume->leftGain << ',' << volume->rightGain << '\n';
          } else if (const auto* expression = std::get_if<ExpressionPerformanceEvent>(&event)) {
            controls << prefix << "expression," << expression->linearGain << '\n';
          } else if (const auto* instrument = std::get_if<InstrumentPerformanceEvent>(&event)) {
            controls << prefix << "instrument," << std::get<InstrumentIdentity>(instrument->instrument).key << '\n';
          }
        }
      }
    }
    std::cout << entry.path().filename().string() << " notes=" << notes
              << " ticks=" << performance.tracks.front().endTick
              << " diagnostics=" << diagnostics.size() + performance.diagnostics.size() << '\n';
    for (const auto& d : diagnostics) {
      std::cout << "  " << d.message << '\n';
    }
    for (const auto& d : performance.diagnostics) {
      std::cout << "  " << d.message << '\n';
    }
  }
  std::cout << "recognized=" << recognized << " rejected=" << rejected << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    detectionValidatesRevisionAndSong();
    timingAndOrderIdentityArePreserved();
    prefixAndCachedDurationControlParsing();
    partialBarrierPrimesTheNextStream();
    revisionVolumeAndSlidesUseDriverArithmetic();
    samplesAreDiscoveredWithoutTheDspDirectory();
    capturedGainRampsContinueOncePerDriverTick();
    retriggersCutReleaseTailsButReleasesAndSlidesDoNot();
    invalidNotesCancelPendingRetriggers();
    gainEnvelopesControlAutomaticRelease();
    zeroPitchIsSilentAndRecoversOnTheNextNote();
    alternatingSpeedsFollowEachRevision();
    portamentoKeepsFractionAndSnapsToItsTarget();
    nullInstrumentsAndConditionalOperandsPreserveTheStream();
    regionsUseFirstMatchAndRetainStateOnAMiss();
    retainedRegionsKeepAPlayableKeyAcrossOverlaps();
    pitchArithmeticPreservesRevisionSpecificWrapping();
    malformedCommandsAndInstrumentsAreBounded();
    if (argc >= 2) {
      corpus(argv[1], argc >= 3 ? argv[2] : "");
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

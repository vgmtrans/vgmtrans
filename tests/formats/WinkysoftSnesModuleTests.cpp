/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../PerformanceTestSupport.h"
#include "../TestSupport.h"
#include "ValueFormatCorpus.h"
#include "value/formats/WinkysoftSnes/WinkysoftSnes.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::winkysoft_snes;

namespace {

struct Fixture {
  std::vector<u8> bytes = std::vector<u8>(kAramSize);

  explicit Fixture(Version version = Version::Standard) {
    const bool early = version == Version::Early || version == Version::Jojo;
    write(0x9000, {0xf7,8,0x30,0,0xc4,0,0x5d,0x3a,8,0xf5,0,0x70,0x5d,0xf7,8,0x1f,0x80,0x70});
    write(0x9040, {0x8d,0x12,0xe8,0,0xda,0x0a,0x8d,0x40,0xe8,0,0xda,0x0c});
    write(0x9060, {0xf6,1,0x30,0xc4,1,0xf6,0,0x30,0xc4,0,0xf6,3,0x30,0x2d,0xf6,2,0x30,0xee,0x9a,0});
    write(0x9080, {0xf5,0,0x31,0xcf,0xcb,0xf3,0xeb,0,0xf5,0x10,0x31,0xcf});
    if (early) {
      write(0x90a0, {0xf6,0,0x71,0xc5,0xf2,0,0xf6,0x10,0x71,0xc5,0xf3,0,0xfc,0xad,15});
    } else {
      write(0x90a0, {0xf6,0,0x71,0xc4,0xf2,0xf6,0x10,0x71,0xc4,0xf3,0xfc,0xad,15});
    }
    for (u32 i = 0; i < 0x69; ++i) { bytes[0x7000 + i] = 2; }
    write(0x7069, {0x36,0x20,0x34,0x32,0x30,0x2e,0x10,0x22,0x26,0x1a,0x1e,0x16,0x18,0x12,0x14,0x2c,0x2a,0x0e,0x0c,0x0a,8,6,4});
    for (u32 i = 0; i < 30; ++i) { word(0x7080 + i * 2, 0x7200 + i * 16); }
    if (early) {
      bytes[0x7069] = 0;
      if (version == Version::Early) { bytes[0x706a] = 0; word(0x7092, 0); }
      bytes[0x18] = 125;
      bytes[0x15] = 8;
      bytes[0x27] = 8;
      write(0x4000, {0,125,255,0,0,0x60});
    } else {
      bytes[0x22] = 125;
      bytes[0x2d] = 255;
      bytes[0x16] = 0x80;
      write(0x90c0, {0x8d,0x60,0xe8,0,0xda,0,0x8f,0xff,8,0x8f,0xff,9,
                     0x0b,0x18,0xf8,0x18,0xf5,0,0x40,0xc4,0x1e,0xc4,0x22,0xc4,0x26,
                     0xf5,1,0x40,0xc4,0x1f,0xc4,0x2d});
      write(0x4000, {125,255});
    }
    if (version == Version::Extended) {
      bytes[0x7067] = 0x3a;
      bytes[0x7068] = 0x38;
      write(0x73c0, {0x1c,0x5d,0xf5,0,0x32,0xc4,0,0xf5,1,0x32,0xf8});
    }
    write(0x7100, {0x4d,0x0d,0x2c,0x3c,0x6c,0x4c,0x5c,0x3d,0x2d,0x5d,0x6d,0x7d,0x0c,0x1c,0});
    write(0x7110, {0,0,0,0,0x20,0,0,0,0,0x20,0xff,0,0x7f,0x7f,0});
    constexpr std::array<u16, 13> pitches{0x085f,0x08de,0x0965,0x09f4,0x0a8c,0x0b2c,0x0bd6,
                                          0x0c8b,0x0d4a,0x0e14,0x0eea,0x0fcd,0x10be};
    for (u32 i = 0; i < pitches.size(); ++i) { word(0x3000 + i * 2, pitches[i]); }
    for (u32 i = 0; i < 16; ++i) { bytes[0x3100 + i] = 255 - i * 8; bytes[0x3110 + i] = 127 + i * 8; }
    write(0x1200, {0,0xff,0xe0,1,0,255,64,0});
    write(0x1208, {0,0x8f,0xe0,2,0,128,32,12});
    word(0x2000, 0x2100); word(0x2002, 0x2100); bytes[0x2100] = 3;
    bytes[0x6000] = 0x78;
  }

  void write(u32 offset, std::initializer_list<u8> values) { std::ranges::copy(values, bytes.begin() + offset); }
  void word(u32 offset, u16 value) { bytes[offset] = value; bytes[offset + 1] = value >> 8; }
  ByteReader reader() const { return ByteReader(SourceId{701}, bytes); }
  SequenceParse parse(std::vector<Diagnostic>* diagnostics = nullptr) const {
    const auto layout = findLayout(reader());
    expect(layout.has_value(), "Winkysoft fixture must be recognized");
    return decodeSequence(reader(), *layout, readDriverData(reader(), *layout), AssetId{701}, nullptr, diagnostics);
  }
};

void recognizesRelocatedProfilesAndRejectsBrokenTables() {
  for (const auto version : {Version::Early, Version::Jojo, Version::Standard, Version::Extended}) {
    Fixture fixture(version);
    const auto layout = findLayout(fixture.reader());
    expect(layout && layout->version == version && layout->songStart == 0x6000 &&
               layout->instrumentTable == 0x1200 && layout->directory == 0x2000 && layout->initialTempo == 125,
           "driver signatures must recover relocated tables and version-specific song loaders");
    fixture.bytes[0x707f] = 0xff;
    expect(!findLayout(fixture.reader()), "corrupt dispatch slots must be rejected before reading handlers");
  }
}

void lookaheadLengthsAndRepeatedControllersStayAligned() {
  Fixture fixture;
  fixture.write(0x6000, {0x7b,0,0x72,64,0,0x24,0xc0,3,4,0x25,0x7d,80,
                        0x70,0xc0,2,0xc1,0x40,3,0x26,0x78});
  std::vector<Diagnostic> diagnostics;
  const auto parsed = fixture.parse(&diagnostics);
  fixture.bytes.clear();
  fixture.bytes.shrink_to_fit();
  const auto performance = SequenceVm().render(parsed.program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(diagnostics.empty() && performance.diagnostics.empty() && notes.size() == 3,
         "packed timing, note lookahead, and implicit controller bytes must all decode");
  expect(notes[0]->header.tick == 0 && notes[1]->header.tick == 4 && notes[2]->header.tick == 15 &&
             performance.tracks[0].endTick == 18,
         "implicit high-bit controller bytes reuse length, while the final low byte reads a new length");
}

void launchesAndRestartsTracksAtRuntime() {
  Fixture fixture;
  fixture.write(0x6000, {0x7f,2,0x6e,1,0,0x61,0x7f,3,0x6e,1,0x20,0x61,0x7f,5,0x78});
  fixture.write(0x6100, {0x7b,0,0x72,64,0,0x30,0xc0,20,20,0x78});
  fixture.write(0x6120, {0x7b,1,0x72,64,0,0x32,0xc0,1,2,0x78});
  const auto parsed = fixture.parse();
  const auto performance = SequenceVm().render(parsed.program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[1]);
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->header.tick == 2 &&
             notes[1]->header.tick == 5 && notes[0]->durationTicks == 3,
         "a track launch must run at the caller's time and replacement must release the previous voice");
}

void launchesLowerChannelsOnTheFollowingFrame() {
  Fixture fixture;
  fixture.write(0x6000, {0x6e,1,0,0x61,0x78});
  fixture.write(0x6100, {0x6e,0,0,0x62,0x78});
  fixture.write(0x6200, {0x7b,0,0x72,64,0,0x30,0xc0,1,2,0x78});
  const auto performance = SequenceVm().render(fixture.parse().program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes[0]->header.tick == 1,
         "a launch of an already-visited channel must wait until the next hardware frame");
}

void lowPitchCorrectionBorrowsFromTheSemitone() {
  Fixture fixture;
  fixture.write(0x6000, {0x7b,0,0x72,64,0,0x0f,0xc0,1,2,0x78});
  const auto layout = *findLayout(fixture.reader());
  const auto data = readDriverData(fixture.reader(), layout);
  const auto performance = SequenceVm().render(fixture.parse().program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(notes.size() == 1, "low-key fixture must produce one note");
  const double pitch = 4096 * std::exp2((notes.front()->key - unityKey(data.instruments[0], data)) / 12);
  expect(std::abs(pitch - 158) < 0.0001,
         "source key 15 with unity tuning must produce DSP pitch 158, including the low-key borrow");
}

void tempoRampsUseTheCarryAndVersionSpecificCadence() {
  for (const auto version : {Version::Early, Version::Standard}) {
    Fixture fixture(version);
    fixture.write(0x6000, {0x7b,0,0x72,64,0,0x79,144,3,0x40,0xc0,6,8,0x40,0x40,0x40,0x40,0x40,0x78});
    const auto performance = SequenceVm().render(fixture.parse().program);
    const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
    const std::array<u64, 6> expected = version == Version::Early
        ? std::array<u64, 6>{0,8,15,23,30,37} : std::array<u64, 6>{0,7,14,21,28,35};
    expect(performance.diagnostics.empty() && notes.size() == expected.size(), "tempo ramp must keep all notes");
    for (size_t i = 0; i < expected.size(); ++i) {
      expect(notes[i]->header.tick == expected[i], "tempo ramp must preserve the driver's carry and frame cadence");
    }
  }
}

void panSequencesAndNoiseRetainTheirState() {
  Fixture fixture(Version::Extended);
  fixture.word(0x3200, 0x3300);
  fixture.write(0x3300, {0xf0,2,0xff,3,0});
  fixture.write(0x6000, {0x7b,0,0x72,64,0,0x68,0,0x6b,31,0xc0,6,8,0x6b,32,0x30,0x78});
  const auto parsed = fixture.parse();
  const auto performance = SequenceVm().render(parsed.program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(parsed.noiseRates == std::set<u8>{31} && notes.size() == 2 && notes[0]->instrument &&
             std::get<InstrumentIdentity>(*notes[0]->instrument).key == noiseInstrumentKey(0, 31) &&
             notes[0]->key == 60 && !notes[1]->instrument,
         "noise notes must use fixed-pitch noise instruments and noise-off must restore sample playback");
  const auto balances = eventsOfType<StereoBalancePerformanceEvent>(performance.tracks[0]);
  expect(std::ranges::any_of(balances, [](const auto* event) { return event->header.tick == 2; }),
         "pan sequence duration bytes count hardware frames");
}

void vibratoMatchesEmulatedPitchWords() {
  // Captured from the Villgust and SRW4 SPC drivers with patch pitch $0100,
  // source key $30, depth 12, and the default speed $20.
  constexpr std::array<u16, 21> early{1120,1128,1146,1154,1162,1154,1136,1128,1112,1104,1086,
                                      1078,1070,1078,1096,1104,1120,1128,1146,1154,1162};
  constexpr std::array<u16, 21> modern{1071,1072,1073,1074,1075,1073,1072,1071,1070,1069,1068,
                                       1067,1066,1067,1068,1069,1071,1072,1073,1074,1075};
  for (const auto version : {Version::Early, Version::Standard}) {
    Fixture fixture(version);
    fixture.write(0x6000, {0x7b,0,0x72,64,0,0x71,12,0,0x30,0xc0,20,24,0x78});
    const auto layout = *findLayout(fixture.reader());
    const auto data = readDriverData(fixture.reader(), layout);
    const auto performance = SequenceVm().render(fixture.parse().program);
    const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
    const auto bends = eventsOfType<PitchBendPerformanceEvent>(performance.tracks[0]);
    expect(notes.size() == 1 && performance.diagnostics.empty(), "vibrato fixture must render one sustained voice");
    const auto& expected = version == Version::Early ? early : modern;
    for (size_t tick = 0; tick < expected.size(); ++tick) {
      double bend = 0;
      for (const auto* event : bends) {
        if (event->header.tick <= tick) { bend = event->semitones; }
      }
      const double pitch = 4096 * std::exp2((notes[0]->key + bend - unityKey(data.instruments[0], data)) / 12);
      expect(std::abs(pitch - expected[tick]) < 0.0001,
             "vibrato must retain the early multiplier behavior and late low-byte/carry behavior");
    }
  }
}

void repeatsCallsAndPercussionPreserveDriverState() {
  Fixture fixture;
  fixture.write(0x6000, {0x7b,0,0x72,64,0,0x76,0,0x61,0x74,0x30,0xc0,1,2,0x75,3,
                        0x6f,1,0xc0,1,2,0x78});
  fixture.write(0x6100, {0x7e,1,0x7f,2,0x77});
  const auto parsed = fixture.parse();
  const auto performance = SequenceVm().render(parsed.program);
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks[0]);
  expect(performance.diagnostics.empty() && notes.size() == 4 && notes[0]->header.tick == 2 &&
             notes[3]->header.tick == 8 && parsed.instruments.contains(1),
         "calls share duration state, finite repeats count total plays, and drum notes select instruments");
  const auto layout = *findLayout(fixture.reader());
  const auto data = readDriverData(fixture.reader(), layout);
  const double drumKey = unityKey(data.instruments[1], data) + 12 * std::log2(512.0 / 4096.0);
  expect(std::abs(notes.back()->key - drumKey) < 0.0001,
         "percussion uses the instrument pitch word directly rather than melodic note transposition");
}

void declaredLoopsUseTheExportLoopPolicy() {
  Fixture fixture;
  fixture.write(0x6000, {0x7b,0,0x72,64,0,0x74,0x30,0xc0,1,2,0x75,0});
  const auto parsed = fixture.parse();
  const auto once = SequenceVm().render(parsed.program);
  const auto twice = SequenceVm(SequenceVmOptions{.sequenceLoops = 1}).render(parsed.program);
  expect(once.diagnostics.empty() && twice.diagnostics.empty() && once.tracks[0].endTick == 2 &&
             twice.tracks[0].endTick == 4,
         "infinite driver repeats must honor the shared VM's additional-loop setting");
}

void truncatedOperandsCannotExecuteOrLaunchTracks() {
  for (const auto opcode : {0x67,0x69,0x6d,0x6e,0x70,0x76,0x79,0x7b,0x7c,0x80}) {
    Fixture fixture(Version::Extended);
    auto layout = *findLayout(fixture.reader());
    layout.songStart = 0xffff;
    fixture.bytes.back() = opcode;
    std::vector<Diagnostic> diagnostics;
    const auto parsed = decodeSequence(fixture.reader(), layout, readDriverData(fixture.reader(), layout),
                                       AssetId{701}, nullptr, &diagnostics);
    expect(!diagnostics.empty() && parsed.program.tracks[0].commands.size() == 1 &&
               parsed.program.tracks[0].commands.front().flow.endsPlayback(),
           "truncated operands must produce one terminal diagnostic command");
  }
}

void scanBuildsSelfContainedBankAndExports() {
  Fixture fixture;
  fixture.write(0x6000, {0x7b,1,0x72,64,0,0x30,0xc0,1,2,0x6b,31,0xc0,1,2,0x78});
  Session session;
  session.registerFormat(module());
  session.scanSource(session.addSource(SourceFile{.name = "Winkysoft fixture.aram",
                                                  .knownFormat = source_formats::kSnesAram}, fixture.bytes));
  const auto snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1 && snapshot.diagnostics().empty(),
         "scanning must publish an explicitly bound sequence and BRR bank");
  const auto artifacts = session.exportCollection(snapshot.collections().front().id,
      ExportRequest{.kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Dls}});
  expect(artifacts.size() == 3 && std::ranges::all_of(artifacts, [](const Artifact& artifact) {
    return !artifact.bytes.empty() && artifact.diagnostics.empty();
  }), "Winkysoft instruments and source-free playback must export MIDI, SF2, and DLS");
}

void runTests() {
  recognizesRelocatedProfilesAndRejectsBrokenTables();
  lookaheadLengthsAndRepeatedControllersStayAligned();
  launchesAndRestartsTracksAtRuntime();
  launchesLowerChannelsOnTheFollowingFrame();
  lowPitchCorrectionBorrowsFromTheSemitone();
  tempoRampsUseTheCarryAndVersionSpecificCadence();
  panSequencesAndNoiseRetainTheirState();
  vibratoMatchesEmulatedPitchWords();
  repeatsCallsAndPercussionPreserveDriverState();
  declaredLoopsUseTheExportLoopPolicy();
  truncatedOperandsCannotExecuteOrLaunchTracks();
  scanBuildsSelfContainedBankAndExports();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc > 1) {
      return vgmtrans::tests::scanValueFormatArchive(argv[1], {
          .format = "WinkysoftSnes",
          .exports = ExportRequest{.kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Dls},
                                    .sequence = {.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 0}},
          .outputDirectory = argc > 2 ? std::optional<std::filesystem::path>{argv[2]} : std::nullopt,
          .requireSoundBank = true});
    }
    runTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

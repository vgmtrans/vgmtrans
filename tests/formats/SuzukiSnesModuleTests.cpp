/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiSnes/SuzukiSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::suzuki_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void addCommonDriverTables(std::vector<u8>& bytes) {
  writeBytes(bytes, 0x0300, {0x8f, 0x5d, 0xf2, 0x8f, 0x60, 0xf3});
  writeBytes(bytes, 0x0400,
             {0xd6, 0x48, 0x01, 0x5d, 0xf5, 0x00, 0x50, 0x1c, 0x5d, 0xf5, 0x01, 0x51, 0xd6, 0x60,
              0x01, 0xeb, 0x23, 0xf5, 0x00, 0x52, 0xd6, 0x78, 0x01, 0xf5, 0x01, 0x52, 0xd6, 0x79,
              0x01, 0xf5, 0x00, 0x53, 0xd6, 0xa8, 0x01, 0xf5, 0x01, 0x53, 0xd6, 0xa9, 0x01});

  std::fill(bytes.begin() + 0x5000, bytes.begin() + 0x5080, 0xff);
  bytes[0x5000] = 0;
  bytes[0x5101] = 0x40;
  bytes[0x5200] = 0x8f;
  bytes[0x5201] = 0xe0;
  // Signed little-endian 8.8 value -0.5 semitones.
  bytes[0x5300] = 0x80;
  bytes[0x5301] = 0xff;

  writeLe16(bytes, 0x6000, 0x6100);
  writeLe16(bytes, 0x6002, 0x6100);
  bytes[0x6100] = 0x01;  // one terminal BRR block
}

void addSd3SongLoader(std::vector<u8>& bytes, u16 header) {
  writeBytes(bytes, 0x0100,
             {0xfa, 0xf5, 0x5c, 0xfa, 0x5c, 0xf5, 0x3f, 0x0f, 0x0a, 0xcd, 0x00, 0xe4, 0x1a, 0x1c,
              0xfd, 0xf5, static_cast<u8>(header), static_cast<u8>(header >> 8), 0xd6, 0x79, 0x1b, 0xf5,
              0x01, 0x20, 0xd6, 0x7a, 0x1b, 0x3d, 0x3d});
}

void addLaterSongLoader(std::vector<u8>& bytes, u16 header) {
  writeBytes(bytes, 0x0100,
             {0xfa, 0xf5, 0x5f, 0x3f, 0xfe, 0x09, 0x3f, 0x8a, 0x04, 0x8f, 0x08, 0x06, 0xe4, 0x1d,
              0x1c, 0x5d, 0xf6, static_cast<u8>(header), static_cast<u8>(header >> 8), 0xd5, 0x4c, 0x1b,
              0xf6, 0x01, 0x20, 0xd5, 0x4d, 0x1b, 0x3d, 0x3d});
}

void addLaterDispatch(std::vector<u8>& bytes, u16 lengths) {
  writeBytes(bytes, 0x0700,
             {0x80, 0xa8, 0xc4, 0x2d, 0x5d, 0xf5, static_cast<u8>(lengths), static_cast<u8>(lengths >> 8),
              0x28, 0x07, 0xc4, 0x06, 0x8d, 0x00, 0xcd, 0x00, 0x8b, 0x06, 0xf0, 0x09, 0xf7, 0x29,
              0xd4, 0x0e, 0x3a, 0x29, 0x3d, 0x2f, 0xf3, 0xae, 0x1c, 0x5d, 0x60, 0xeb, 0x1e, 0x1f,
              0xa9, 0x16});
}

std::vector<u8> sd3Fixture() {
  constexpr u16 header = 0x2000;
  constexpr u16 track = 0x3000;
  std::vector<u8> bytes(kAramSize);
  addSd3SongLoader(bytes, header);
  addCommonDriverTables(bytes);
  writeLe16(bytes, header, track);
  writeBytes(bytes, header + 16, {0x02, 0x00, 0x40, 0x40, 0x80, 0x80});
  writeBytes(bytes, track, {0xde, 0x00, 0xee, 0xa8, 0xef, 0xd0});
  return bytes;
}

std::vector<u8> laterFixture(bool smr) {
  constexpr u16 header = 0x2000;
  constexpr u16 track = 0x3000;
  constexpr u16 lengths = 0x7000;
  std::vector<u8> bytes(kAramSize);
  addLaterSongLoader(bytes, header);
  addCommonDriverTables(bytes);
  addLaterDispatch(bytes, lengths);
  bytes[lengths + 56] = smr ? 4 : 1;
  writeBytes(bytes, header, {0x02, 0x00, 0x40, 0x40, 0x80, 0x80});
  writeLe16(bytes, header + 6, track);
  bytes[track] = 0xd0;
  return bytes;
}

template <class Event>
std::vector<const Event*> events(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

PerformanceSequence render(Version version, std::vector<u8> bytes) {
  const auto& dialect = sequenceDialect();
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .behavior = dialect.defaultBehavior,
      .tracks = {decodeSourceTrack(ByteReader(SourceId{121}, bytes), version, 0, 0)},
  };
  program.behavior.initialTempoMicrosecondsPerQuarter = version == Version::SeikenDensetsu3 ? 576000 : 372000;
  return SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
}

void layoutsAndHeadersAreVersioned() {
  const auto sd3 = findLayout(ByteReader(SourceId{122}, sd3Fixture()));
  expect(sd3 && sd3->version == Version::SeikenDensetsu3 && sd3->sequenceHeaderAddress == 0x2000 &&
             sd3->srcnTableAddress == 0x5000 && sd3->volumeTableAddress == 0x5101,
         "SD3 signature should recover the song header and all four split instrument tables");

  const auto bl = findLayout(ByteReader(SourceId{123}, laterFixture(false)));
  const auto smr = findLayout(ByteReader(SourceId{124}, laterFixture(true)));
  expect(bl && bl->version == Version::BahamutLagoon && smr && smr->version == Version::SuperMarioRpg,
         "the FC operand length should distinguish SMR from the otherwise shared Bahamut Lagoon driver");

  const SequenceParse sd3Sequence = decodeSequence(ByteReader(SourceId{125}, sd3Fixture()), *sd3, AssetId{125});
  const SequenceParse blSequence = decodeSequence(ByteReader(SourceId{126}, laterFixture(false)), *bl, AssetId{126});
  expect(sd3Sequence.program.tracks.size() == 1 && sd3Sequence.headerRange.size == 22 &&
             sd3Sequence.recipes.drums.size() == 1 && sd3Sequence.recipes.drums.front().sourceProgram == 0,
         "SD3 should decode pointers before its sequence-owned drum recipe");
  expect(blSequence.program.tracks.size() == 1 && blSequence.headerRange.size == 22 &&
             blSequence.recipes.drums.size() == 1 && blSequence.program.tracks.front().startAddress.value == 0x3000,
         "later drivers should decode the same immutable recipe before their track pointers");
}

void playbackUsesAuditedGatingPitchAndLoops() {
  const PerformanceSequence gated = render(Version::SeikenDensetsu3, {
                                                                            0xdd,
                                                                            0x08,
                                                                            0xcf,
                                                                            0x08,
                                                                            0xec,
                                                                            0x01,
                                                                            0xde,
                                                                            0x03,
                                                                            0xee,
                                                                            0xa8,
                                                                            0xef,
                                                                            0xd0,
                                                                        });
  const auto notes = events<NotePerformanceEvent>(gated.tracks.front());
  const auto tunings = events<TuningPerformanceEvent>(gated.tracks.front());
  const auto instruments = events<InstrumentPerformanceEvent>(gated.tracks.front());
  expect(gated.diagnostics.empty() && notes.size() == 1 && notes.front()->key == 60.0 &&
             notes.front()->durationTicks == 2,
         "duration rate 8 should gate a three-tick percussion note after two ticks");
  expect(!tunings.empty() && std::abs(tunings.back()->cents - 75.0) < 0.000001,
         "CF sixteenth-semitone tuning and EC quarter-semitone transpose should retain their fractions");
  expect(instruments.size() >= 4 && instruments[instruments.size() - 2]->sourceInstrument->key == kDrumKitKey &&
             instruments.back()->sourceInstrument->key == 3,
         "percussion mode should select the derived kit and restore the last melodic program afterward");

  const PerformanceSequence repeated = render(Version::SeikenDensetsu3, {
                                                                               0xd4,
                                                                               0x02,
                                                                               0xa8,
                                                                               0xd6,
                                                                               0xc4,
                                                                               0xd5,
                                                                               0xd0,
                                                                           });
  const auto repeatedNotes = events<NotePerformanceEvent>(repeated.tracks.front());
  expect(repeated.diagnostics.empty() && repeatedNotes.size() == 2 && repeatedNotes[0]->key == 72.0 &&
             repeatedNotes[1]->key == 72.0,
         "repeat break should branch only on the final pass and repeat end should restore the saved octave");
}

void scannerBuildsSequenceDerivedDrumKit() {
  Session session;
  session.registerFormat(definition());
  session.addSource(SourceFile{.name = "SuzukiSnes fixture.aram"}, sd3Fixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "SuzukiSnes fixture should publish one complete source collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.sequence && collection.members.instrumentSets.size() == 1 &&
             collection.members.sampleCollections.size() == 1,
         "SuzukiSnes collection should connect its sequence, instruments, and BRR samples");

  const auto* set = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(set != nullptr && set->instruments.size() == 2,
         "one melodic program and its sequence-derived drum kit should share one immutable instrument set");
  const auto kit = std::ranges::find_if(set->instruments, [](const Instrument& instrument) {
    return instrument.identity && instrument.identity->key == kDrumKitKey;
  });
  expect(kit != set->instruments.end() && kit->regions.size() == 1,
         "the decoded sequence recipe should materialize one drum region during scanning");
  const Region& drum = kit->regions.front();
  expect(drum.keyRange == KeyRange{.low = 62, .high = 62} && std::abs(drum.unityKey - 67.5) < 0.000001 &&
             std::abs(drum.pan - 0.5) < 0.000001 && std::abs(drum.attenuationDb - 6.020599913) < 0.000001,
         "drum key remapping, signed 8.8 tuning, center pan, and 7-bit gain should match the SPC driver");
}

}  // namespace

void runSuzukiSnesModuleTests() {
  layoutsAndHeadersAreVersioned();
  playbackUsesAuditedGatingPitchAndLoops();
  scannerBuildsSequenceDerivedDrumKit();
}

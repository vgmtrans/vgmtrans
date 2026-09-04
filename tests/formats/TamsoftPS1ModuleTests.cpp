/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TamsoftPS1/TamsoftPS1.h"

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
using namespace vgmtrans::formats::tamsoft_ps1;

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

std::vector<u8> sfx(std::vector<u8> events) {
  std::vector<u8> bytes(8 + events.size(), 0);
  le16(bytes, 0, 1);
  le16(bytes, 2, 8);
  le32(bytes, 4, 0x00fffff0);
  std::ranges::copy(events, bytes.begin() + 8);
  return bytes;
}

std::vector<u8> bgm(Generation generation) {
  const u32 records = generation == Generation::Ps2 ? 48 : 24;
  const u32 header = 8;
  const u32 track = header + records * 4;
  std::vector<u8> bytes(track + 1, 0);
  le16(bytes, 2, static_cast<u16>(header));
  le32(bytes, 4, 0x00fffff0);
  for (u32 index = 0; index < records; ++index) {
    le16(bytes, header + index * 4 + 2, static_cast<u16>(records * 4));
  }
  bytes[track] = 0xff;
  return bytes;
}

std::vector<u8> bank(Generation generation) {
  constexpr u32 sampleSize = 0x30;
  std::vector<u8> bytes(0x800 + sampleSize, 0);
  le32(bytes, 4, 0x10);       // program 1
  le32(bytes, 0x3fc, sampleSize);
  le32(bytes, 0x404, generation == Generation::Ps2 ? 0xd2f2e11e : 0xdfe080ff);
  bytes[0x810] = 0x11;
  bytes[0x811] = 1;
  return bytes;
}

template <class Event>
std::vector<const Event*> events(const PerformanceTrack& track) {
  std::vector<const Event*> found;
  for (const auto& event : track.events) {
    if (const auto* value = std::get_if<Event>(&event)) {
      found.push_back(value);
    }
  }
  return found;
}

void layoutsDistinguishDriverGenerationsAndPlayedTracks() {
  const auto ps1 = readSequenceLayouts(ByteReader(SourceId{300}, bgm(Generation::Ps1)));
  const auto ps2 = readSequenceLayouts(ByteReader(SourceId{301}, bgm(Generation::Ps2)));
  expect(ps1.size() == 1 && ps1.front().generation == Generation::Ps1 && ps1.front().tracks.size() == 24,
         "PS1 BGM headers should execute all 24 driver work records");
  expect(ps2.size() == 1 && ps2.front().generation == Generation::Ps2 && ps2.front().headerSize == 0xc0 &&
             ps2.front().tracks.size() == 36,
         "HG2 should retain its 48-record header while executing the 36 records used by reqmus");

  const auto ps1Bank = readBankLayout(ByteReader(SourceId{302}, bank(Generation::Ps1)));
  const auto ps2Bank = readBankLayout(ByteReader(SourceId{303}, bank(Generation::Ps2)));
  expect(ps1Bank && ps1Bank->generation == Generation::Ps1 && ps2Bank && ps2Bank->generation == Generation::Ps2,
         "native TVB contents should distinguish direct PS1 ADSR words from HG2's stored encoding");

  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "BGM.TVB"}, bank(Generation::Ps2));
  session.scanPendingSources();
  const auto snapshot = session.snapshot();
  const auto found = std::ranges::find_if(snapshot.assets(), [](const Asset& asset) {
    return std::holds_alternative<SoundBankAsset>(asset);
  });
  expect(found != snapshot.assets().end(), "HG2's native .TVB should publish a sound bank without being renamed");
  const auto& soundBank = std::get<SoundBankAsset>(*found);
  expect(soundBank.instruments.size() == 1 &&
             soundBank.instruments.front().regions.front().envelope ==
                 psxSpuEnvelope(0x1eee, 0x0d0d, PsxSpuGeneration::Ps2),
         "load_tvbf's complemented ADSR mask should be applied before SPU2 envelope conversion");
}

void sequenceUsesAuditedMixerPitchReverbAndDelaySemantics() {
  const auto bytes = sfx({
      0xe0, 100,              // track volume
      0xe1, 64, 32,           // independent left/right gains
      0xe2, 5,                // tone, and pitch-scale reset
      0xea, 0x00, 0x10,       // unity pitch scale
      0xb0,                    // key 48
      1,
      0xe4, 0x00, 0x20,       // attack-free pitch change to key 60
      1,
      0xe5, 0x00, 0x10,       // retrigger by direct pitch at key 48
      0xe8,                    // wet voice routing
      0xe7, 0x40,             // half-scale DSP depth
      0xe6, 3,                // reverb mode
      1,
      0xf0,
      0xff,
  });
  const ByteReader reader(SourceId{304}, bytes);
  const auto layouts = readSequenceLayouts(reader);
  expect(layouts.size() == 1, "audited Tamsoft SFX fixture should parse");
  const PerformanceSequence performance =
      SequenceVm(LoopPolicy::PlayOnce).render(parseSequence(reader, AssetId{304}, layouts.front()));
  expect(performance.diagnostics.empty() && performance.tracks.size() == 1,
         "audited Tamsoft fixture should render one clean driver voice");
  const auto levels = events<LevelPerformanceEvent>(performance.tracks.front());
  const auto balances = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(levels.size() == 2 && std::abs(levels.back()->linearGain - 6400.0 / 16383.0) < 0.000001,
         "E0 should retain the SPU register's 0x3fff full-scale gain");
  expect(balances.size() == 2 && balances.back()->leftGain == 1.0 && balances.back()->rightGain == 0.5,
         "E1 should remain two independent gain lanes rather than an invented pan law");

  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(notes.size() == 3 && std::abs(notes[0]->key - 48.0) < 0.001 &&
             std::abs(notes[1]->key - 60.0) < 0.001 && std::abs(notes[2]->key - 48.0) < 0.001 &&
             notes[1]->header.tick == 1 && notes[2]->header.tick == 2,
         "key, direct pitch change, and pitch retrigger should preserve the driver's attack behavior");
  const auto reverbs = events<ReverbPerformanceEvent>(performance.tracks.front());
  expect(reverbs.size() == 4 && reverbs[1]->send == 0.5 && reverbs.back()->filterIndex == 3,
         "track routing, signed depth, and reverb mode should remain explicit");

  const auto zeroWait = sfx({0, 0xb0, 1, 0xf0, 0xff});
  const ByteReader zeroReader(SourceId{305}, zeroWait);
  const auto zeroLayout = readSequenceLayouts(zeroReader);
  const auto zeroPerformance =
      SequenceVm(LoopPolicy::PlayOnce).render(parseSequence(zeroReader, AssetId{305}, zeroLayout.front()));
  expect(events<NotePerformanceEvent>(zeroPerformance.tracks.front()).front()->header.tick == 65'536,
         "a zero delta should reproduce the signed 16-bit counter's 65,536-tick wrap");
}

void externalChannelBecomesAnInheritedDelayedTrack() {
  const auto bytes = sfx({
      3,
      0xe0, 100,
      0xf9, 4, 0,
      0xb0, 2, 0xf0, 0xff,
      0xe2, 7, 0xb1, 1, 0xf0, 0xff,
  });
  const ByteReader reader(SourceId{306}, bytes);
  const auto layouts = readSequenceLayouts(reader);
  const SequenceProgram program = parseSequence(reader, AssetId{306}, layouts.front());
  expect(program.tracks.size() == 2, "a forward F9 should materialize one cloned driver voice");
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.tracks.size() == 2 &&
             events<NotePerformanceEvent>(performance.tracks[1]).front()->header.tick == 3,
         "the cloned voice should begin at the spawn tick and execute its relative target");
  const auto cloneLevels = events<LevelPerformanceEvent>(performance.tracks[1]);
  expect(!cloneLevels.empty() && std::abs(cloneLevels.front()->linearGain - 6400.0 / 16383.0) < 0.000001,
         "F9 should inherit the source voice's current mixer state");
}

void modulePairsPs1MusicAndSfxBanksByRole() {
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "C27BGM.TSQ", .path = "/fixture/C27/C27BGM.TSQ"},
                    bgm(Generation::Ps1));
  session.addSource(SourceFile{.name = "C27.TSQ", .path = "/fixture/C27/C27.TSQ"},
                    sfx({0xe2, 1, 0xb0, 1, 0xf0, 0xff}));
  session.addSource(SourceFile{.name = "C27.TVB", .path = "/fixture/C27/C27.TVB"},
                    bank(Generation::Ps1));
  session.addSource(SourceFile{.name = "BGM.TVB", .path = "/fixture/SYS/BGM.TVB"},
                    bank(Generation::Ps1));
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const auto pairedBank = [&](std::string_view collectionName) -> const SoundBankAsset* {
    const auto collection = std::ranges::find_if(snapshot.collections(), [&](const Collection& candidate) {
      return candidate.name.starts_with(collectionName);
    });
    return collection == snapshot.collections().end() || collection->members.soundBanks.size() != 1
               ? nullptr
               : snapshot.asset<SoundBankAsset>(collection->members.soundBanks.front());
  };
  const auto* music = pairedBank("C27BGM");
  const auto* sfxBank = pairedBank("C27 (");
  expect(music != nullptr && music->metadata.name == "BGM" && sfxBank != nullptr && sfxBank->metadata.name == "C27",
         "PS1 music should use the global BGM bank while stage SFX uses its exact same-directory bank");
}

}  // namespace

void runTamsoftPs1ModuleTests() {
  layoutsDistinguishDriverGenerationsAndPlayedTracks();
  sequenceUsesAuditedMixerPitchReverbAndDelaySemantics();
  externalChannelBecomesAnInheritedDelayedTrack();
  modulePairsPs1MusicAndSfxBanksByRole();
}

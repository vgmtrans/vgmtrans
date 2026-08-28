/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionBinding.h"
#include "value/extractors/PsfExtractor.h"
#include "value/formats/SquarePS2/SquarePS2.h"
#include "value/session/Session.h"
#include "value/synth/PsxSpu.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::square_ps2;

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

std::vector<u8> bgmFixture() {
  const std::vector<u8> events{
      0x00, 0x21, 0x80, 0x00,        // WD 128, program 0
      0x00, 0x08, 0x78,              // tempo 120
      0x00, 0x11, 0x3c, 0x64,        // note on
      0x00, 0x40, 0x20, 0x04, 0x00,  // vibrato
      0x00, 0x48, 0x30, 0x06, 0x01,  // tremolo
      0x00, 0x50, 0x40, 0x08, 0x02,  // pan LFO
      0x00, 0x30,                    // ADSR reset
      0x00, 0x31, 0x60,              // dynamic attack rate
      0x00, 0x60,                    // wet routing on
      0x00, 0x28, 0x04,              // four-tick portamento
      0x08, 0x11, 0x3e, 0x60,        // fresh second note glides from key 60
      0x04, 0x1a, 0x3c,              // release key 60 at the end of its glide
      0x04, 0x2a,                    // legato on
      0x00, 0x12, 0x40,              // overlapping third note glides from key 62
      0x04, 0x1a, 0x3e,              // release key 62 at the end of its glide
      0x04, 0x1a, 0x40,              // release key 64
      0x00, 0x2b,                    // legato off
      0x00, 0x29,                    // portamento off
      0x00, 0x00,                    // end
  };
  const u32 length = 0x20 + 4 + static_cast<u32>(events.size());
  std::vector<u8> bytes(length, 0);
  bytes[0] = 'B';
  bytes[1] = 'G';
  bytes[2] = 'M';
  bytes[3] = ' ';
  le16(bytes, 4, 12);
  le16(bytes, 6, 128);
  bytes[8] = 1;
  le16(bytes, 0x0a, 120);
  bytes[0x0c] = 127;
  le16(bytes, 0x0e, 48);
  le32(bytes, 0x10, length);
  le32(bytes, 0x20, static_cast<u32>(events.size()));
  std::ranges::copy(events, bytes.begin() + 0x24);
  return bytes;
}

std::vector<u8> wdFixture() {
  constexpr u32 sampleOffset = 0x90;
  std::vector<u8> bytes(sampleOffset + 0x60, 0);
  bytes[0] = 'W';
  bytes[1] = 'D';
  le16(bytes, 2, 128);
  le32(bytes, 4, 0x60);
  le32(bytes, 8, 2);
  le32(bytes, 0x0c, 3);
  le32(bytes, 0x20, 0x30);
  le32(bytes, 0x24, 0x70);

  // A simple stereo program can carry the first-region flag even though its
  // two records end at the next instrument pointer.
  le32(bytes, 0x30, 0x10101);
  le32(bytes, 0x34, 0);
  le32(bytes, 0x38, 0);
  le16(bytes, 0x3c, composePsxAdsr1(1, 0x60, 8, 8));
  le16(bytes, 0x3e, composePsxAdsr2(1, 1, 0x40, 1, 0x10));
  le16(bytes, 0x42, 0x0100);
  bytes[0x44] = 127;
  bytes[0x45] = 127;
  bytes[0x46] = 127;
  bytes[0x47] = 0xc0;
  bytes[0x48] = 2;

  le32(bytes, 0x50, 1);
  le32(bytes, 0x54, 0x20);
  le16(bytes, 0x62, 0x0100);
  bytes[0x64] = 127;
  bytes[0x65] = 127;
  bytes[0x66] = 127;
  bytes[0x67] = 0xff;

  le32(bytes, 0x70, 0x200);
  le32(bytes, 0x74, 0x40);
  le32(bytes, 0x78, 0x10);
  le16(bytes, 0x7c, composePsxAdsr1(1, 0x50, 7, 7));
  le16(bytes, 0x7e, composePsxAdsr2(1, 1, 0x30, 1, 0x0f));
  le16(bytes, 0x82, 0x0100);
  bytes[0x84] = 127;
  bytes[0x85] = 127;
  bytes[0x86] = 127;
  bytes[0x87] = 0xc0;

  bytes[sampleOffset] = 0x11;
  bytes[sampleOffset + 1] = 0;
  bytes[sampleOffset + 0x10] = 0x11;
  bytes[sampleOffset + 0x11] = 1;
  bytes[sampleOffset + 0x20] = 0x11;
  bytes[sampleOffset + 0x21] = 0;
  bytes[sampleOffset + 0x30] = 0x11;
  bytes[sampleOffset + 0x31] = 1;
  bytes[sampleOffset + 0x40] = 0x11;
  bytes[sampleOffset + 0x41] = 0;
  bytes[sampleOffset + 0x50] = 0x11;
  bytes[sampleOffset + 0x51] = 3;
  return bytes;
}

std::vector<u8> compress(std::span<const u8> bytes) {
  uLongf size = compressBound(bytes.size());
  std::vector<u8> result(size);
  expect(compress2(result.data(), &size, bytes.data(), bytes.size(), Z_BEST_COMPRESSION) == Z_OK,
         "fixture compression should succeed");
  result.resize(size);
  return result;
}

std::vector<u8> psf2Fixture() {
  struct Member {
    std::string name;
    std::vector<u8> bytes;
  };
  const std::vector<Member> members{{"music.bgm", bgmFixture()}, {"bank.wd", wdFixture()}};
  std::vector<u8> psf(20 + members.size() * 48, 0);
  psf[0] = 'P';
  psf[1] = 'S';
  psf[2] = 'F';
  psf[3] = 2;
  le32(psf, 16, static_cast<u32>(members.size()));
  for (u32 index = 0; index < members.size(); ++index) {
    const auto compressed = compress(members[index].bytes);
    const u32 entry = 20 + index * 48;
    std::ranges::copy(members[index].name, psf.begin() + entry);
    const u32 node = static_cast<u32>(psf.size());
    le32(psf, entry + 36, node);
    le32(psf, entry + 40, static_cast<u32>(members[index].bytes.size()));
    le32(psf, entry + 44, static_cast<u32>(members[index].bytes.size()));
    psf.resize(psf.size() + 20 + compressed.size(), 0);
    le32(psf, node + 16, static_cast<u32>(compressed.size()));
    std::ranges::copy(compressed, psf.begin() + node + 20);
  }
  le32(psf, 4, static_cast<u32>(psf.size() - 16));
  return psf;
}

template <class Event>
size_t countEvents(const PerformanceSequence& performance) {
  size_t count = 0;
  for (const auto& track : performance.tracks) {
    count += std::ranges::count_if(track.events,
                                   [](const PerformanceEvent& event) { return std::holds_alternative<Event>(event); });
  }
  return count;
}

const Collection* squareCollection(const SessionSnapshot& snapshot) {
  for (const auto& collection : snapshot.collections()) {
    if (!collection.members.sequence) {
      continue;
    }
    const auto* sequence = snapshot.asset<SequenceProgramAsset>(*collection.members.sequence);
    if (sequence != nullptr && sequence->metadata.format == kSquarePs2FormatName) {
      return &collection;
    }
  }
  return nullptr;
}

Session scan(std::string name, std::filesystem::path path, std::vector<u8> bytes) {
  Session session;
  session.registerExtractor(vgmtrans::formats::psf::psfExtractor());
  session.registerFormat(module());
  session.addSource(SourceFile{.name = std::move(name), .path = std::move(path)}, std::move(bytes));
  session.scanPendingSources();
  return session;
}

void syntheticArchiveCoversDriverFeatures() {
  Session session = scan("fixture.psf2", "/fixture/fixture.psf2", psf2Fixture());
  const SessionSnapshot snapshot = session.snapshot();
  expect(std::ranges::none_of(snapshot.diagnostics(),
                              [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; }),
         "synthetic SquarePS2 scan should not produce validation errors");
  expect(snapshot.sources().size() == 3, "PSF2 extraction should publish both filesystem members");
  const Collection* collection = squareCollection(snapshot);
  expect(collection != nullptr && collection->members.soundBanks.size() == 1,
         "BGM and WD members should resolve through their driver bank ID");

  const auto binding = bindCollection(snapshot, collection->id);
  expect(binding.collection.has_value(), "resolved SquarePS2 collection should bind");
  expect(binding.collection->soundBanks().size() == 1, "bound collection should retain one WD bank");
  const auto& bank = binding.collection->soundBanks().front();
  expect(bank.instruments.size() == 2 && bank.instruments[0].regions.size() == 2 &&
             bank.instruments[1].regions.size() == 1 && bank.localSamples.samples.size() == 3,
         "a first-region stereo pair should stop at the next WD instrument pointer");
  expect(bank.instruments[0].regions[0].pan == 0.0 && bank.instruments[0].regions[1].pan == 1.0,
         "WD stereo partners should use the driver's hard-left and hard-right routing");
  expect(bank.instruments[0].explicitAddress && bank.instruments[0].explicitAddress->bank == 0 &&
             bank.instruments[0].explicitAddress->program == 0,
         "WD IDs should resolve instruments without becoming MIDI or SoundFont bank numbers");
  expect(std::abs(bank.instruments.front().regions.front().unityKey - 59.0) < 0.0001,
         "WD signed 8.8 pitch correction should determine the unity key");
  const auto& relativeLoop = bank.instruments[1].regions.front().loop;
  expect(relativeLoop && relativeLoop->enabled && relativeLoop->start == 28 && relativeLoop->length == 28,
         "WD loop offsets should be relative to their sample start");
  expect(bank.instruments.front().reverb == 1.0, "WD routing overrides should retain wet-send capability");

  const auto rendered = renderCollection(*binding.collection,
                                         SequenceRenderOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 0});
  expect(rendered.performance.has_value(), "SquarePS2 collection should render");
  const auto& performance = *rendered.performance;
  expect(countEvents<NotePerformanceEvent>(performance) == 3, "split note-on/off commands should form three notes");
  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks.front().events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 3 && notes[0]->durationTicks == 8 && notes[1]->header.tick == 8 &&
             notes[1]->durationTicks == 12 && notes[2]->header.tick == 16,
         "non-legato portamento should end its source note while explicit legato preserves overlap");
  expect(countEvents<EnvelopePerformanceEvent>(performance) >= 2,
         "ADSR reset and live attack-rate writes should emit dynamic envelope events");
  expect(countEvents<ModulationPerformanceEvent>(performance) >= 6,
         "vibrato, tremolo, and pan LFO should each retain physical depth and rate events");
  const auto& automations = performance.tracks.front().automations;
  const auto* firstSlide = automations.empty() ? nullptr : pitchTransitionIntent(automations.front());
  const auto* secondSlide = automations.size() < 2 ? nullptr : pitchTransitionIntent(automations[1]);
  const auto driverSlide = [](const PitchTransitionIntent* slide, double startKey, double targetKey) {
    return slide && slide->startKey == startKey && slide->targetKey == targetKey &&
           slide->portamentoRendering.required;
  };
  expect(automations.size() == 2 && driverSlide(firstSlide, 60.0, 62.0) && driverSlide(secondSlide, 62.0, 64.0),
         "SquarePS2 slides should retain their persistent driver pitch and MIDI portamento requirement");
  const size_t reverbEvents = countEvents<ReverbPerformanceEvent>(performance);
  const bool wetRouting = std::ranges::any_of(performance.tracks, [](const PerformanceTrack& track) {
    return std::ranges::any_of(track.events, [](const PerformanceEvent& event) {
      const auto* reverb = std::get_if<ReverbPerformanceEvent>(&event);
      return reverb != nullptr && reverb->send == 1.0;
    });
  });
  expect(reverbEvents >= 1 && wetRouting, "SPU2 effect routing should emit a full wet-send event");
  const auto playback = session.preparePlayback(
      collection->id, PlaybackRequest{.sequence = {.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 0}});
  expect(playback.playable(), "SquarePS2 playback preparation should produce both MIDI and SoundFont data");
}

std::vector<u8> readFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  expect(static_cast<bool>(stream), "real PSF2 archive should be readable");
  const auto size = stream.tellg();
  expect(size >= 0, "real PSF2 archive size should be readable");
  stream.seekg(0);
  std::vector<u8> bytes(static_cast<size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  expect(static_cast<bool>(stream), "real PSF2 archive should be read completely");
  return bytes;
}

void scanRealArchive(const std::filesystem::path& path) {
  Session session = scan(path.filename().string(), path, readFile(path));
  const SessionSnapshot snapshot = session.snapshot();
  expect(std::ranges::none_of(snapshot.diagnostics(),
                              [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; }),
         "real SquarePS2 scan should not produce validation errors");
  const Collection* collection = squareCollection(snapshot);
  if (collection == nullptr || collection->members.soundBanks.empty()) {
    std::cerr << "real archive diagnostic: " << snapshot.sources().size() << " sources, " << snapshot.assets().size()
              << " assets, " << snapshot.collections().size() << " collections\n";
    for (const auto& source : snapshot.sources()) {
      std::cerr << "source " << source.id.value << " " << source.name << " size=" << source.size << '\n';
      if (source.name.ends_with(".bgm")) {
        const ByteReader reader = session.sources().reader(source.id);
        std::cerr << "BGM header:";
        for (u32 index = 0; index < std::min<u64>(reader.size(), 0x30); ++index) {
          std::cerr << ' ' << std::hex << static_cast<unsigned>(reader.u8At(index));
        }
        std::cerr << std::dec << '\n';
      }
    }
    for (const auto& asset : snapshot.assets()) {
      std::visit(
          [](const auto& value) {
            std::cerr << "asset " << value.metadata.format << " " << value.metadata.name << '\n';
          },
          asset);
    }
    for (const auto& diagnostic : snapshot.diagnostics()) {
      std::cerr << "diagnostic: " << diagnostic.message << '\n';
    }
  }
  expect(collection != nullptr && !collection->members.soundBanks.empty(),
         "real SquarePS2 archive should resolve a BGM with its WD bank");
  const auto binding = bindCollection(snapshot, collection->id);
  expect(binding.collection.has_value(), "real SquarePS2 collection should bind");
  const auto playback = session.preparePlayback(
      collection->id, PlaybackRequest{.sequence = {.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 0}});
  expect(playback.playable() && countEvents<NotePerformanceEvent>(playback.performance) != 0,
         "real SquarePS2 BGM should prepare playable MIDI and SoundFont data");
  const auto* sequence = snapshot.asset<SequenceProgramAsset>(*collection->members.sequence);
  size_t commands = 0;
  if (sequence != nullptr) {
    for (const auto& track : sequence->program.tracks) {
      commands += track.commands.size();
    }
  }
  std::cout << path.filename().string() << ": " << snapshot.sources().size() << " sources, "
            << binding.collection->soundBanks().front().instruments.size() << " instruments, "
            << (sequence == nullptr ? 0 : sequence->program.tracks.size()) << " tracks, " << commands << " commands, "
            << countEvents<NotePerformanceEvent>(playback.performance) << " notes\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    syntheticArchiveCoversDriverFeatures();
    if (argc > 1) {
      scanRealArchive(argv[1]);
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "SquarePS2 test failure: " << ex.what() << '\n';
    return 1;
  }
}

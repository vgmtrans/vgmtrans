/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/CueExtractor.h"
#include "value/formats/ValueFormats.h"
#include "value/session/Session.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace vgmtrans::core;
using vgmtrans::formats::cue::cueExtractor;

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct Fixture {
  std::filesystem::path directory = std::filesystem::temp_directory_path() /
      ("vgmtrans-cue-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  Fixture() { std::filesystem::create_directories(directory / "tracks"); }
  ~Fixture() {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }

  void write(const std::string& name, std::span<const u8> bytes) const {
    std::ofstream file(directory / name, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    expect(bool(file), "fixture write failed");
  }

  void cue(const std::string& text) const {
    write("disc.cue", {reinterpret_cast<const u8*>(text.data()), text.size()});
  }

  ExtractionResult extract() const {
    Session session;
    const auto id = session.addSourceFromPath(directory / "disc.cue");
    return cueExtractor().extract({session.sources().source(id), session.sources().reader(id)});
  }
};

std::vector<u8> sectors(size_t sectorSize, std::initializer_list<bool> forms) {
  std::vector<u8> bytes;
  u8 value = 1;
  for (bool form2 : forms) {
    const auto offset = bytes.size();
    bytes.resize(offset + sectorSize, 0xee);
    const size_t header = sectorSize == 2352 ? 16 : 0;
    if (header != 0) {
      bytes[offset + 15] = 2;
    }
    bytes[offset + header + 2] = bytes[offset + header + 6] = form2 ? 0x20 : 0;
    std::fill_n(bytes.begin() + offset + header + 8, form2 ? 2324 : 2048, value++);
  }
  return bytes;
}

void extractsBothSectorLayoutsAndForms() {
  Fixture fixture;
  for (const size_t sectorSize : {2352, 2336}) {
    fixture.write("tracks/data track.bin", sectors(sectorSize, {false, true, false}));
    fixture.cue("\xef\xbb\xbfREM ignored metadata\r\nfile \"tracks\\data track.bin\" binary\r\n"
                " track 01 mode2/" + std::to_string(sectorSize) + "\r\n  index 01 00:00:00\r\n");
    const auto result = fixture.extract();
    expect(result.diagnostics.empty() && result.sources.size() == 1, "both MODE2 layouts should extract");
    std::vector<u8> expected(2048, 1);
    expected.insert(expected.end(), 2324, 2);
    expected.insert(expected.end(), 2048, 3);
    expect(result.sources.front().bytes == expected, "only complete Form 1 and Form 2 user data should remain");
    expect(result.sources.front().file.path == fixture.directory / "disc.cue", "derived path should retain its cue");
    expect(!result.sources.front().file.knownFormat, "track bytes should permit normal format discovery");
  }
}

void respectsFilesTrackBoundariesAndPregaps() {
  Fixture fixture;
  fixture.write("disc.bin", sectors(2352, {false, false, false, false, false, false, false}));
  fixture.write("second.bin", sectors(2336, {true}));
  fixture.cue("FILE disc.bin BINARY\n"
              " TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n"
              " TRACK 02 MODE2/2352\n INDEX 00 00:00:01\n INDEX 01 00:00:02\n INDEX 02 00:00:03\n"
              " TRACK 03 AUDIO\n INDEX 00 00:00:04\n INDEX 01 00:00:05\n"
              " TRACK 04 MODE2/2352\n INDEX 01 00:00:06\n"
              "FILE missing-audio.wav WAVE\n TRACK 05 AUDIO\n INDEX 01 00:00:00\n"
              "FILE second.bin BINARY\n TRACK 06 MODE2/2336\n PREGAP 00:02:00\n INDEX 01 00:00:00\n");
  const auto result = fixture.extract();
  expect(result.diagnostics.empty() && result.sources.size() == 3, "all MODE2 tracks and only those should load");
  std::vector<u8> expected(2048, 3);
  expected.insert(expected.end(), 2048, 4);
  expect(result.sources[0].bytes == expected, "INDEX 01 starts data; the next INDEX 00 ends it");
  expect(result.sources[1].bytes == std::vector<u8>(2048, 7), "later tracks must not duplicate earlier data");
  expect(result.sources[2].bytes == std::vector<u8>(2324, 1), "file indexes reset and synthetic pregaps add no bytes");
}

void reportsInvalidInputsAndContinuesOtherFiles() {
  Fixture fixture;
  fixture.write("disc.bin", sectors(2352, {false, false}));
  for (const auto* directives : {
           "TRACK 01 MODE2/2352\n",
           "TRACK 01 MODE2/2352\n INDEX 00 00:00:00\n",
           "TRACK 01 MODE2/2352\n INDEX 01 00:00:02\n",
           "TRACK 01 MODE2/2352\n INDEX 00 00:00:01\n INDEX 01 00:00:00\n",
           "TRACK 01 MODE2/2352\n INDEX 01 00:00:01\n TRACK 02 AUDIO\n INDEX 01 00:00:00\n",
           "TRACK 01 MODE2/2048\n INDEX 01 00:00:00\n"}) {
    fixture.cue(std::string("FILE disc.bin BINARY\n") + directives);
    const auto result = fixture.extract();
    expect(result.sources.empty() && !result.diagnostics.empty(), "invalid track layouts must report a diagnostic");
  }
  for (const auto* text : {
           "FILE \"unterminated.bin BINARY\n",
           "TRACK 01 MODE2/2352\n",
           "FILE disc.bin BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:60:00\n",
           "FILE disc.bin BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:75\n",
           "FILE disc.bin BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n INDEX 01 00:00:00\n"}) {
    fixture.cue(text);
    Session session;
    session.registerExtractor(cueExtractor());
    const auto id = session.addSourceFromPath(fixture.directory / "disc.cue");
    session.scanSource(id);
    expect(!session.snapshot().diagnostics().empty(), "malformed cues should become session diagnostics");
  }
  auto damaged = sectors(2352, {false});
  damaged.pop_back();
  fixture.write("truncated.bin", damaged);
  damaged = sectors(2352, {false});
  damaged[15] = 1;
  fixture.write("mode1.bin", damaged);
  fixture.cue("FILE missing.bin BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n"
              "FILE truncated.bin BINARY\n TRACK 02 MODE2/2352\n INDEX 01 00:00:00\n"
              "FILE mode1.bin BINARY\n TRACK 03 MODE2/2352\n INDEX 01 00:00:00\n"
              "FILE disc.bin WAVE\n TRACK 04 MODE2/2352\n INDEX 01 00:00:00\n"
              "FILE disc.bin BINARY\n TRACK 05 MODE2/2352\n INDEX 01 00:00:00\n");
  const auto result = fixture.extract();
  expect(result.sources.size() == 1 && result.diagnostics.size() == 4, "bad files must not prevent loading good files");
  fixture.cue("FILE absent.wav WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n");
  expect(!fixture.extract().diagnostics.empty(), "audio-only cues should explain why nothing was loaded");
}

void routesBinPathsAndScansDerivedSources() {
  Fixture fixture;
  fixture.write("disc.bin", sectors(2352, {false}));
  fixture.cue("FILE disc.bin BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n");
  Session registry;
  vgmtrans::formats::registerValueFormats(registry);
  expect(std::ranges::any_of(registry.formats().extractors(), [](const SourceExtractor& extractor) {
    return extractor.name == "Cue";
  }), "normal format registration should include CUE support");

  for (const auto* name : {"disc.cue", "disc.bin", "disc.BiN"}) {
    Session session;
    session.registerExtractor(cueExtractor());
    size_t scans = 0;
    session.registerFormat(FormatModule{
        .name = "TrackProbe",
        .scan = [&](const ScanInput& input) {
          ++scans;
          expect(input.source.derived() && input.reader.size() == 2048, "scanners should see only extracted payloads");
          ScanResult result;
          result.diagnostics.push_back(Diagnostic{.severity = Severity::Info, .message = "track inspected"});
          return result;
        },
    });
    const auto id = session.addSourceFromPath(fixture.directory / name);
    expect(session.sources().source(id).path == fixture.directory / "disc.cue",
           "BIN paths should load their sibling CUE");
    session.scanSource(id);
    expect(scans == 1 && session.sources().sourceCount() == 2, "extraction must consume the cue without recursing");
    const auto diagnostics = session.snapshot().diagnostics();
    expect(diagnostics.size() == 1 && diagnostics.front().severity == Severity::Info,
           "scanning extracted tracks should not report errors or warnings");
    expect(session.sources().source(SourceId{1}).parent == id, "extracted tracks must belong to the cue source family");
    session.removeSource(id);
    expect(session.sources().sourceCount() == 0, "removing the cue must remove its tracks");
  }
  std::filesystem::remove(fixture.directory / "disc.cue");
  Session session;
  const auto id = session.addSourceFromPath(fixture.directory / "disc.bin");
  expect(session.sources().reader(id).size() == 2352 && !session.sources().source(id).knownFormat,
         "a BIN without a sibling cue should retain ordinary source loading");
}

}  // namespace

int main() {
  try {
    extractsBothSectorLayoutsAndForms();
    respectsFilesTrackBoundariesAndPregaps();
    reportsInvalidInputsAndContinuesOtherFiles();
    routesBinPathsAndScansDerivedSources();
    std::cout << "CUE tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}

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

void extractsMode1AndPayloadOnlyTracks() {
  Fixture fixture;
  struct Layout {
    const char* mode;
    size_t sectorSize;
    size_t offset;
    size_t payloadSize;
  };
  for (const auto& layout : {Layout{"MODE1/2048", 2048, 0, 2048}, Layout{"MODE1/2352", 2352, 16, 2048},
                             Layout{"MODE2/2048", 2048, 0, 2048}, Layout{"MODE2/2324", 2324, 0, 2324}}) {
    std::vector<u8> bytes(layout.sectorSize * 2, 0xee);
    std::vector<u8> expected;
    for (size_t i = 0; i < 2; ++i) {
      if (layout.sectorSize == 2352) {
        bytes[i * layout.sectorSize + 15] = 1;
      }
      // Payload bytes resembling XA submode flags must remain ordinary data.
      const auto value = static_cast<u8>(0x20 + i);
      std::fill_n(bytes.begin() + i * layout.sectorSize + layout.offset, layout.payloadSize, value);
      expected.insert(expected.end(), layout.payloadSize, value);
    }
    fixture.write("disc.bin", bytes);
    fixture.cue("FILE disc.bin BINARY\n TRACK 01 " + std::string(layout.mode) + "\n INDEX 01 00:00:00\n");
    const auto result = fixture.extract();
    expect(result.diagnostics.empty() && result.sources.size() == 1, "every Mode 1 and payload-only mode should load");
    expect(result.sources.front().bytes == expected, "fixed payloads must retain every byte and exclude raw overhead");
    bytes.pop_back();
    fixture.write("disc.bin", bytes);
    const auto truncated = fixture.extract();
    expect(truncated.sources.empty() && !truncated.diagnostics.empty(),
           "partial sectors must be rejected in every mode");
  }
}

void respectsFilesTrackBoundariesAndPregaps() {
  Fixture fixture;
  auto bytes = sectors(2352, {false, false, false, false, false, false, false});
  bytes[15] = 1;
  std::fill_n(bytes.begin() + 16, 2048, 1);
  fixture.write("disc.bin", bytes);
  fixture.write("second.bin", sectors(2336, {true}));
  fixture.cue("FILE disc.bin BINARY\n"
              " TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n"
              " TRACK 02 MODE2/2352\n INDEX 00 00:00:01\n INDEX 01 00:00:02\n INDEX 02 00:00:03\n"
              " TRACK 03 AUDIO\n INDEX 00 00:00:04\n INDEX 01 00:00:05\n"
              " TRACK 04 MODE2/2352\n INDEX 01 00:00:06\n"
              "FILE missing-audio.wav WAVE\n TRACK 05 AUDIO\n INDEX 01 00:00:00\n"
              "FILE second.bin BINARY\n TRACK 06 MODE2/2336\n PREGAP 00:02:00\n INDEX 01 00:00:00\n");
  const auto result = fixture.extract();
  expect(result.diagnostics.empty() && result.sources.size() == 4,
         "all data tracks should load and audio should be skipped");
  expect(result.sources[0].bytes == std::vector<u8>(2048, 1), "Mode 1 data must load before neighboring Mode 2 tracks");
  std::vector<u8> expected(2048, 3);
  expected.insert(expected.end(), 2048, 4);
  expect(result.sources[1].bytes == expected, "INDEX 01 starts data; the next INDEX 00 ends it");
  expect(result.sources[2].bytes == std::vector<u8>(2048, 7), "later tracks must not duplicate earlier data");
  expect(result.sources[3].bytes == std::vector<u8>(2324, 1), "file indexes reset and synthetic pregaps add no bytes");
}

void supportsDifferentSectorSizesInOneFile() {
  Fixture fixture;
  std::vector<u8> bytes(2048, 0xee);  // Leading stored pregap.
  bytes.insert(bytes.end(), 2048, 1);
  bytes.insert(bytes.end(), 2336, 0xee);  // Next track's stored pregap uses its own sector size.
  const auto xa = sectors(2336, {true, false});
  bytes.insert(bytes.end(), xa.begin(), xa.end());
  bytes.insert(bytes.end(), 2352, 0xee);  // Audio.
  bytes.insert(bytes.end(), 2324, 3);
  fixture.write("disc.bin", bytes);
  fixture.cue("FILE disc.bin BINARY\n"
              " TRACK 01 MODE1/2048\n INDEX 00 00:00:00\n INDEX 01 00:00:01\n"
              " TRACK 02 MODE2/2336\n INDEX 00 00:00:02\n INDEX 01 00:00:03\n"
              " TRACK 03 AUDIO\n INDEX 01 00:00:05\n"
              " TRACK 04 MODE2/2324\n INDEX 01 00:00:06\n");
  const auto result = fixture.extract();
  expect(result.diagnostics.empty() && result.sources.size() == 3,
         "sector sizes may change between tracks in one file");
  std::vector<u8> expected(2324, 1);
  expected.insert(expected.end(), 2048, 2);
  expect(result.sources[0].bytes == std::vector<u8>(2048, 1) && result.sources[1].bytes == expected &&
             result.sources[2].bytes == std::vector<u8>(2324, 3),
         "byte offsets must account for preceding tracks and pregaps at their stored sector sizes");
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
           "TRACK 01 MODE1/2324\n INDEX 01 00:00:00\n",
           "TRACK 01 MODE0/2352\n INDEX 01 00:00:00\n"}) {
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
    extractsMode1AndPayloadOnlyTracks();
    respectsFilesTrackBoundariesAndPregaps();
    supportsDifferentSectorSizesInOneFile();
    reportsInvalidInputsAndContinuesOtherFiles();
    routesBinPathsAndScansDerivedSources();
    std::cout << "CUE tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}

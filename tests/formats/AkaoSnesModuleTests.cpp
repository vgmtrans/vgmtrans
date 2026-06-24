/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnesLayout.h"
#include "value/formats/AkaoSnes/AkaoSnesSequence.h"
#include "value/formats/ValueFormats.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::akao_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

template <size_t Size>
void writeBytes(std::vector<u8>& bytes, size_t offset, const std::array<u8, Size>& values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<u8> makeAkaoSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 10> readNoteLengthV1{0xcd, 0x0f, 0x8d, 0x00, 0x9e, 0xf8, 0x27, 0xf6, 0xb1, 0x18};
  writeBytes(bytes, 0x0100, readNoteLengthV1);

  constexpr std::array<u8, 20> vcmdExecFF4{0xa8, 0xd2, 0x1c, 0xfd, 0xf6, 0xee, 0x17, 0x2d, 0xf6, 0xed,
                                           0x17, 0x2d, 0xdd, 0x5c, 0xfd, 0xf6, 0x49, 0x18, 0xf0, 0x0a};
  writeBytes(bytes, 0x0200, vcmdExecFF4);
  writeLe16(bytes, 0x0200 + 9, 0x1900);
  writeLe16(bytes, 0x0200 + 16, 0x1800);

  constexpr std::array<u8, 46> ff4Lengths{0x03, 0x03, 0x01, 0x02, 0x03, 0x03, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01,
                                          0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x02, 0x03,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  writeBytes(bytes, 0x1800, ff4Lengths);

  constexpr std::array<u8, 20> readSeqHeaderV1{0x8d, 0x01, 0xcb, 0x8d, 0xcd, 0x00, 0xf5, 0x00, 0x20, 0xd4,
                                               0x02, 0xf5, 0x01, 0x20, 0xd4, 0x03, 0xf0, 0x0a, 0xdb, 0x48};
  writeBytes(bytes, 0x0300, readSeqHeaderV1);
  writeLe16(bytes, 0x0300 + 7, 0x2000);

  constexpr std::array<u8, 7> loadDirV1{0xe8, 0x1e, 0x8d, 0x5d, 0x3f, 0xe9, 0x10};
  writeBytes(bytes, 0x0400, loadDirV1);
  bytes[0x0400 + 1] = 0x50;

  constexpr std::array<u8, 11> loadInstrV1{0xd5, 0xc1, 0x02, 0xfd, 0xf6, 0x00, 0xff, 0xd5, 0x00, 0x03, 0x6f};
  writeBytes(bytes, 0x0500, loadInstrV1);
  bytes[0x0500 + 6] = 0x52;

  writeLe16(bytes, 0x2000, 0x2100);
  for (size_t i = 1; i < 8; ++i) {
    writeLe16(bytes, 0x2000 + i * 2, 0);
  }
  bytes[0x2100] = 0xda;
  bytes[0x2101] = 5;
  bytes[0x2102] = 0xdb;
  bytes[0x2103] = 0;
  bytes[0x2104] = 0x00;
  bytes[0x2105] = 0xf1;

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x5200] = 0;
  bytes[0x6000] = 0x01;

  return bytes;
}

}  // namespace

void akaoSnesLayoutDiscoversFf4StyleAram() {
  const auto bytes = makeAkaoSnesAram();
  const auto layout = findAkaoSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "AkaoSnes synthetic FF4 ARAM should match scanner patterns");
  expect(layout->version == AKAOSNES_V1, "synthetic FF4 ARAM should be classified as AkaoSnes V1");
  expect(layout->minorVersion == AKAOSNES_V1_FF4, "synthetic FF4 ARAM should be classified as FF4 minor version");
  expect(layout->sequenceHeaderAddress == 0x2000, "sequence header address should come from legacy V1 header reader");
  expect(layout->spcDirAddress == 0x5000, "SPC DIR address should come from legacy V1 DIR loader");
  expect(layout->tuningTableAddress == 0x5200, "V1 tuning table address should come from legacy instrument loader");
}

void akaoSnesModuleDiscoversSequenceInstrumentsAndSamples() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  const SourceId source = session.addSource(SourceFile{.name = "ff4-test.spc"}, makeAkaoSnesAram());
  static_cast<void>(session.scanSource(source));
  const SessionSnapshot project = session.snapshot();

  expect(project.diagnostics().empty(), "AkaoSnes synthetic scan should not report diagnostics");
  expect(project.collections().size() == 1, "AkaoSnes synthetic scan should produce one collection");
  expect(project.assets().size() == 3, "AkaoSnes synthetic scan should produce sequence, instrument set, and samples");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets()[0]);
  expect(sequence != nullptr, "first AkaoSnes asset should be a sequence");
  expect(sequence->metadata.format == "AkaoSnes", "sequence should retain AkaoSnes format name");
  expect(sequence->program.timebase.ppqn == kAkaoSnesPpqn, "AkaoSnes sequence should use SNES PPQN");
  expect(sequence->program.tracks.size() == 1, "null V1 track pointers should be skipped");

  const auto* dialect = session.dialects().find(sequence->program.dialect.value);
  expect(dialect != nullptr, "registered AkaoSnes dialect should render the scanned sequence");
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, *dialect);
  expect(performance.diagnostics.empty(), "AkaoSnes performance render should not report diagnostics");
  expect(!performance.tracks.empty(), "AkaoSnes performance should contain a track");
  const bool hasNote = std::ranges::any_of(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(hasNote, "AkaoSnes synthetic track should render a note event");

  const auto* instrumentSet = std::get_if<InstrumentSetAsset>(&project.assets()[1]);
  expect(instrumentSet != nullptr, "second AkaoSnes asset should be an instrument set");
  expect(!instrumentSet->instruments.empty(), "AkaoSnes instrument set should contain at least one instrument");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets()[2]);
  expect(samples != nullptr, "third AkaoSnes asset should be a sample collection");
  expect(samples->samples.samples.size() == 1, "AkaoSnes synthetic scan should collect one used BRR sample");
}

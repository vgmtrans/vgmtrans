/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnes.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/formats/ValueFormats.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"

#include "ValueFormatTestSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::konami_snes;

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

const SourceAnnotation* annotationWithKind(const SourceMap& sourceMap, SourceId source, SourceRole role,
                                           std::string_view category) {
  const auto annotations = sourceMap.withRole(source, role);
  for (const SourceAnnotationId id : annotations) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    if (annotation.category() == category) {
      return &annotation;
    }
  }
  return nullptr;
}

template <class Event>
bool hasMidiEvent(const MidiTrack& track) {
  return std::ranges::any_of(track.events, [](const MidiEvent& event) { return std::holds_alternative<Event>(event); });
}

template <class Event>
std::vector<const Event*> performanceEvents(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

bool hasNonZeroPitchBendBefore(const MidiTrack& track, u64 tick) {
  return std::ranges::any_of(track.events, [tick](const MidiEvent& event) {
    const auto* pitchBend = std::get_if<PitchBend>(&event);
    return pitchBend != nullptr && pitchBend->tick < tick && pitchBend->value != 0;
  });
}

bool hasNonZeroPitchBendAtOrAfter(const MidiTrack& track, u64 tick) {
  return std::ranges::any_of(track.events, [tick](const MidiEvent& event) {
    const auto* pitchBend = std::get_if<PitchBend>(&event);
    return pitchBend != nullptr && pitchBend->tick >= tick && pitchBend->value != 0;
  });
}

std::vector<u8> makeKonamiSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 10> setSongHeaderAddressGG4{0x8f, 0x00, 0x0a, 0x8f, 0x20, 0x0b, 0xcd, 0x00, 0xd8, 0x1c};
  writeBytes(bytes, 0x0100, setSongHeaderAddressGG4);

  constexpr std::array<u8, 15> jumpToVcmdGG4{0x1c, 0xfd, 0xf6, 0xbc, 0x1a, 0x2d, 0xf6, 0xbb,
                                             0x1a, 0x2d, 0xf6, 0x00, 0x03, 0xf0, 0x08};
  writeBytes(bytes, 0x0120, jumpToVcmdGG4);
  writeLe16(bytes, 0x0120 + 11, 0x0300);

  constexpr std::array<u8, 6> setDirGG4{0x8f, 0x5d, 0xf2, 0x8f, 0x50, 0xf3};
  writeBytes(bytes, 0x0140, setDirGG4);

  constexpr std::array<u8, 48> loadInstrGG4{0x09, 0x11, 0x10, 0xfd, 0xf5, 0xa1, 0x01, 0xd0, 0x27, 0xdd, 0x68, 0x28,
                                            0xb0, 0x0c, 0x8f, 0x3c, 0x04, 0x8f, 0x0a, 0x05, 0x3f, 0xee, 0x1b, 0x5f,
                                            0xe2, 0x18, 0xa8, 0x28, 0x2d, 0xeb, 0x25, 0xf6, 0x20, 0x0a, 0xc4, 0x04,
                                            0xf6, 0x21, 0x0a, 0xc4, 0x05, 0xae, 0x3f, 0xee, 0x1b, 0x5f, 0xe2, 0x18};
  writeBytes(bytes, 0x0160, loadInstrGG4);
  bytes[0x0160 + 11] = 0x01;
  bytes[0x0160 + 15] = 0x00;
  bytes[0x0160 + 18] = 0x40;
  bytes[0x0160 + 30] = 0x10;
  writeLe16(bytes, 0x0160 + 32, 0x4100);

  constexpr std::array<u8, 13> loadPercInstrGG4{0x8f, 0x00, 0x04, 0x8f, 0x43, 0x05, 0x8d,
                                                0x07, 0xcf, 0x7a, 0x04, 0xda, 0x04};
  writeBytes(bytes, 0x01c0, loadPercInstrGG4);

  bytes[0x0010] = 0x00;
  writeLe16(bytes, 0x4100, 0x4200);
  bytes[0x4200] = 0xff;
  bytes[0x4305] = 0x29;

  writeLe16(bytes, 0x2000, 0x2002);
  bytes[0x2002] = 0xea;
  bytes[0x2003] = 0x80;
  bytes[0x2004] = 0xe2;
  bytes[0x2005] = 0x00;
  bytes[0x2006] = 0xee;
  bytes[0x2007] = 0x7f;
  bytes[0x2008] = 0xe3;
  bytes[0x2009] = 0x14;
  bytes[0x200a] = 0xe4;
  bytes[0x200b] = 0x08;
  bytes[0x200c] = 0x20;
  bytes[0x200d] = 0x10;
  bytes[0x200e] = 0x3c;
  bytes[0x200f] = 0x06;
  bytes[0x2010] = 0x7f;
  bytes[0x2011] = 0x7f;
  bytes[0x2012] = 0xff;

  bytes[0x4000] = 0x00;
  bytes[0x4001] = 0x00;
  bytes[0x4002] = 0x00;
  bytes[0x4003] = 0x8f;
  bytes[0x4004] = 0xe0;
  bytes[0x4005] = 0x14;
  bytes[0x4006] = 0x7f;

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x6000] = 0x01;

  return bytes;
}

void writeKonamiInstrumentEntry(std::vector<u8>& bytes, u32 offset, u8 srcn) {
  constexpr std::array<u8, 6> fields{
      0x00,  // key
      0x00,  // tuning
      0x8f,  // ADSR1
      0xe0,  // ADSR2
      0x14,  // pan
      0x20,  // volume
  };
  bytes[offset] = srcn;
  writeBytes(bytes, offset + 1, fields);
}

void writeKonamiLegacyInstrumentEntry(std::vector<u8>& bytes, u32 offset, u8 srcn) {
  constexpr std::array<u8, 7> fields{
      0x00,  // key
      0x00,  // tuning
      0xff,  // ADSR1
      0xe0,  // ADSR2
      0xb8,  // GAIN
      0x0a,  // pan
      0x00,  // volume
  };
  bytes[offset] = srcn;
  writeBytes(bytes, offset + 1, fields);
}

std::vector<u8> makeBatmanReturnsAram() {
  std::vector<u8> bytes(0x10000);

  // Batman Returns enters its music setup path for table indexes 0x7c and
  // above. Keep earlier rows empty and make row 0x7c playable.
  constexpr std::array<u8, 38> readSongList{
      0xe4, 0x0c, 0x8f, 0xe5, 0x04, 0x8f, 0x03, 0x05, 0x9c, 0x8d, 0x05, 0xcf, 0x7a, 0x04, 0xda, 0x04, 0x8d, 0x00, 0xcd,
      0x00, 0xf7, 0x04, 0xc4, 0x1c, 0xfc, 0xf7, 0x04, 0xc4, 0x06, 0xe4, 0x0c, 0x68, 0x7c, 0x90, 0x03, 0x5f, 0x6d, 0x1b,
  };
  writeBytes(bytes, 0x1aba, readSongList);
  writeLe16(bytes, 0x03e5 + 0x7c * 5 + 3, 0x3900);

  // The V2 dispatcher has two low commands (0x60/0x61) and one-byte command
  // lengths. Its 0xfc entry is two operands, which keeps the following note
  // aligned at 0x3909.
  constexpr std::array<u8, 34> vcmdLengths{
      0x00, 0x00, 0x01, 0x02, 0x01, 0x01, 0x03, 0x03, 0x00, 0x03, 0x00, 0x03, 0x01, 0x02, 0x01, 0x03, 0x01,
      0x02, 0x01, 0x03, 0x01, 0x03, 0x03, 0x03, 0x00, 0x00, 0x02, 0x01, 0x03, 0x01, 0x01, 0x02, 0x02, 0x00,
  };
  writeBytes(bytes, 0x0e60, vcmdLengths);
  constexpr std::array<u8, 16> branchForVcmd6x{
      0xe4, 0x08, 0x8f, 0xde, 0x04, 0x68, 0xe0, 0xb0, 0x0c, 0x8f, 0x60, 0x04, 0x68, 0x62, 0x90, 0x05,
  };
  writeBytes(bytes, 0x0e82, branchForVcmd6x);
  constexpr std::array<u8, 21> jumpToVcmd{
      0x80, 0xa4, 0x04, 0x1c, 0xfd, 0xf6, 0x1d, 0x0e, 0x2d, 0xf6, 0x1c,
      0x0e, 0x2d, 0xdd, 0x5c, 0xfd, 0xf6, 0x60, 0x0e, 0xf0, 0x08,
  };
  writeBytes(bytes, 0x0e97, jumpToVcmd);

  constexpr std::array<u8, 10> setDir{
      0xe8, 0x4c, 0x8d, 0x5d, 0xcc, 0xf2, 0x00, 0xc5, 0xf3, 0x00,
  };
  writeBytes(bytes, 0x097a, setDir);

  // The loader routes programs 0x10-0x18 through the selected bank and returns
  // to the common table at 0x19. The bank pointer is selected by RAM byte 0x26.
  constexpr std::array<u8, 76> loadInstrument{
      0x09, 0x1c, 0x13, 0xd5, 0x96, 0x02, 0x68, 0x19, 0xb0, 0x04, 0x68, 0x10, 0xb0, 0x11, 0xe8, 0x17, 0xc4, 0x04, 0xe8,
      0x07, 0xc4, 0x05, 0xf5, 0x96, 0x02, 0x3f, 0x64, 0x0f, 0x5f, 0xe4, 0x0c, 0xe5, 0x26, 0x00, 0x1c, 0xfd, 0xf6, 0x0d,
      0x07, 0xc4, 0x04, 0xf6, 0x0e, 0x07, 0xc4, 0x05, 0xf5, 0x96, 0x02, 0x80, 0xa8, 0x10, 0x3f, 0x64, 0x0f, 0x5f, 0xe4,
      0x0c, 0xe8, 0xdf, 0xc4, 0x04, 0xe8, 0x07, 0xc4, 0x05, 0xf5, 0x96, 0x02, 0x8d, 0x08, 0xcf, 0x7a, 0x04, 0xda, 0x04,
  };
  writeBytes(bytes, 0x0f1f, loadInstrument);
  bytes[0x0026] = 0x04;
  writeLe16(bytes, 0x070d + 0x04 * 2, 0x08bf);

  // Keep the common prefix sparse, then provide the seven bank rows that fit
  // before the driver entry point. Program 0x17 would begin at 0x08f7, where
  // actual SPC700 opcodes must not be accepted as an instrument row.
  for (u32 program = 0; program < 0x10; ++program) {
    bytes[0x0717 + program * 8] = 0xff;
  }
  writeKonamiLegacyInstrumentEntry(bytes, 0x0717, 0);
  for (u32 program = 0x10; program <= 0x16; ++program) {
    writeKonamiLegacyInstrumentEntry(bytes, 0x08bf + (program - 0x10) * 8, 0);
  }
  constexpr std::array<u8, 8> driverEntry{0x20, 0xcd, 0xcf, 0xbd, 0xe8, 0x30, 0xc5, 0xf1};
  writeBytes(bytes, 0x08f7, driverEntry);

  // Program 0x19 returns to the common table. That same row is percussion note
  // zero; an implausible pan in the next row terminates both packed suffixes.
  writeKonamiLegacyInstrumentEntry(bytes, 0x07df, 0);
  writeKonamiLegacyInstrumentEntry(bytes, 0x07e7, 0);
  bytes[0x07e7 + 6] = 0xff;

  writeLe16(bytes, 0x3900, 0x3902);
  constexpr std::array<u8, 12> track{
      0xea, 0x80, 0xe2, 0x10, 0xfc, 0x00, 0x00, 0x3c, 0x06, 0x7f, 0x7f, 0xff,
  };
  writeBytes(bytes, 0x3902, track);

  writeLe16(bytes, 0x4c00, 0x5000);
  writeLe16(bytes, 0x4c02, 0x5000);
  bytes[0x5000] = 0x01;

  return bytes;
}

std::vector<u8> makeKonamiSnesBuilderAram() {
  std::vector<u8> bytes(0x10000);

  // Programs 1-3 are deliberately empty. Program 4 therefore proves that
  // sparse source programs do not leak into dense annotation ownership.
  writeKonamiInstrumentEntry(bytes, 0x4000, 3);
  bytes[0x4007] = 0xff;
  bytes[0x400e] = 0xff;
  bytes[0x4015] = 0xff;
  writeKonamiInstrumentEntry(bytes, 0x401c, 2);
  writeKonamiInstrumentEntry(bytes, 0x4200, 4);
  bytes[0x4207] = 0xff;

  // Three source entries intentionally join one percussion instrument.
  writeKonamiInstrumentEntry(bytes, 0x4300, 1);
  writeKonamiInstrumentEntry(bytes, 0x4307, 4);
  writeKonamiInstrumentEntry(bytes, 0x430e, 5);
  bytes[0x4315] = 0xff;
  bytes[0x431a] = 0xff;

  const auto directoryEntry = [&](u8 srcn, u16 start) {
    const u32 offset = 0x5000 + static_cast<u32>(srcn) * 4;
    writeLe16(bytes, offset, start);
    writeLe16(bytes, offset + 2, start);
    bytes[start] = 0x01;
  };
  directoryEntry(1, 0x5100);
  directoryEntry(2, 0x5100);  // Explicit alias of SRCN 1.
  directoryEntry(3, 0xa100);
  directoryEntry(4, 0x6200);
  directoryEntry(5, 0x3000);
  // The DSP ignores DIR's loop field when the BRR end block does not loop.
  writeLe16(bytes, 0x5000 + 5 * 4 + 2, 0x0000);
  return bytes;
}

PerformanceSequence renderKonamiSnesTrack(std::span<const u8> commandBytes) {
  std::vector<u8> bytes(commandBytes.begin(), commandBytes.end());
  const auto& config = konamiSnesSequenceConfig(KONAMISNES_V6);
  TrackProgram track = decodeKonamiSnesSourceTrack(ByteReader(SourceId{9}, bytes), KONAMISNES_V6, 0, 0);
  const SequenceProgram program{
      .runtime = konamiSnesSequenceRuntime(KONAMISNES_V6),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {track},
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

PerformanceSequence renderKonamiSnesProgram(KonamiSnesVersion version, const std::vector<std::vector<u8>>& tracks,
                                            u32 sequenceLoops = 0, bool indexedEchoFilter = false) {
  const auto& config = konamiSnesSequenceConfig(version);
  std::vector<TrackProgram> programTracks;
  programTracks.reserve(tracks.size());
  for (u32 trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
    programTracks.push_back(decodeKonamiSnesSourceTrack(ByteReader(SourceId{100 + trackIndex}, tracks[trackIndex]),
                                                        version, trackIndex, 0));
  }
  const SequenceProgram program{
      .runtime = konamiSnesSequenceRuntime(version, indexedEchoFilter),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = std::move(programTracks),
  };
  return SequenceVm(SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = sequenceLoops})
      .render(program);
}

PerformanceSequence renderKonamiSnesAramSequence(const std::vector<u8>& bytes, const KonamiSnesLayout& layout,
                                                 AssetId asset = AssetId{33}) {
  const ByteReader reader(SourceId{asset.value}, bytes);
  const SequenceProgram program =
      decodeKonamiSnesSequence(reader, layout, asset, parseKonamiSnesInstrumentInfos(reader, layout));
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

}  // namespace

void konamiSnesLayoutDiscoversDirectHeaderAndSynthTables() {
  const auto bytes = makeKonamiSnesAram();
  const auto layout = findKonamiSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "KonamiSnes fixture should match the value scanner layout patterns");
  expect(layout->version == KONAMISNES_V6, "direct GG4-style fixture should be classified as KonamiSnes V6");
  expect(layout->sequenceHeaderAddress == 0x2000, "layout should recover the direct sequence header address");
  expect(layout->spcDirAddress == 0x5000, "layout should recover the SPC DIR address");
  expect(layout->commonInstrumentTableAddress == 0x4000, "layout should recover the common instrument table");
  expect(layout->bankedInstrumentTableAddress == 0x4200, "layout should resolve the active banked instrument table");
  expect(layout->percussionInstrumentTableAddress == 0x4300, "layout should recover the percussion table");
}

void konamiSnesLayoutInfersSpcDirFromInstrumentTables() {
  auto bytes = makeKonamiSnesAram();
  std::fill(bytes.begin() + 0x0140, bytes.begin() + 0x0146, u8{0});

  const auto layout = findKonamiSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "KonamiSnes fixture should still match without a DIR write pattern");
  expect(layout->spcDirAddress == 0x5000, "layout should infer SPC DIR from valid instrument sample references");
}

void konamiSnesBatmanReturnsAramUsesV2LayoutAndBoundedBank() {
  const auto aram = makeBatmanReturnsAram();
  const ByteReader reader(SourceId{8}, aram);
  const auto layout = findKonamiSnesLayout(reader);
  expect(layout.has_value(), "Batman Returns selector should be recognized as a KonamiSnes layout");
  expect(layout->version == KONAMISNES_V2, "Batman Returns should use the V2 command set");
  expect(layout->sequenceHeaderAddress == 0x3900,
         "Batman Returns song row 0x7c should resolve the streamed music header");
  expect(layout->spcDirAddress == 0x4c00, "Batman Returns layout should recover the live DSP sample directory");
  expect(layout->commonInstrumentTableAddress == 0x0717 && layout->bankedInstrumentTableAddress == 0x08bf &&
             layout->firstBankedInstrument == 0x10 && layout->bankedInstrumentEnd == 0x19 &&
             layout->percussionInstrumentTableAddress == 0x07df,
         "Batman Returns loader should expose its common, bounded bank, and percussion tables");

  const SourceRange headerRange = konamiSnesSequenceHeaderRange(reader, *layout);
  expect(headerRange.offset == 0x3900 && headerRange.size == 2,
         "Batman Returns streamed header should infer one source track");
  const auto infos = parseKonamiSnesInstrumentInfos(reader, *layout);
  const SequenceProgram program = decodeKonamiSnesSequence(reader, *layout, AssetId{33}, infos);
  expect(program.runtime.valid() && program.tracks.size() == 1,
         "Batman Returns sequence should own an executable decoded track");
  const TrackProgram& track = program.tracks.front();
  expect(track.commands.size() == 5 && track.commands[2].opcode == 0xfc && track.commands[2].range.size == 3 &&
             track.commands[3].opcode == 0x3c && track.commands[3].address.value == 0x3909,
         "V2 opcode 0xfc should consume two operands without misaligning the following note");

  constexpr std::array<u32, 9> expectedMelodicPrograms{0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x19};
  expect(infos.size() == expectedMelodicPrograms.size() + 1,
         "Batman Returns should retain the bounded melodic bank and one percussion row");
  for (size_t index = 0; index < expectedMelodicPrograms.size(); ++index) {
    expect(!infos[index].percussion && infos[index].index == expectedMelodicPrograms[index],
           "Batman Returns melodic instruments should preserve loader program routing");
    expect(infos[index].source.range.offset < 0x08f7,
           "Batman Returns SPC700 driver opcodes must not be parsed as instruments");
  }
  expect(infos.back().percussion && infos.back().percussionNote == 0 && infos.back().source.range.offset == 0x07df,
         "Batman Returns percussion table should remain independently discoverable");
}

void konamiSnesModuleDiscoversSequenceInstrumentsAndSamples() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);

  const SourceId source = session.addSource(SourceFile{.name = "Axelay.spc"}, makeKonamiSnesAram());
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
  expect(project.diagnostics().empty(), "KonamiSnes scan should not report diagnostics for the complete fixture");
  expect(project.collections().size() == 1, "KonamiSnes scan should produce one collection");
  expect(project.assets().size() == 2, "KonamiSnes scan should produce a sequence and sound bank");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets()[0]);
  expect(sequence != nullptr, "first KonamiSnes asset should be a sequence");
  expect(sequence->metadata.format == "KonamiSnes", "sequence should retain format name");
  expect(sequence->metadata.range.offset == 0x2000 && sequence->metadata.range.size == 2,
         "sequence range should cover the inferred one-track header");
  expect(sequence->program.runtime.valid(), "sequence should carry its executable runtime");
  expect(sequence->program.timebase.ppqn == 48, "KonamiSnes sequence should use the SNES value PPQN");
  expect(sequence->program.tracks.size() == 1, "fixture should decode one nonzero source track");

  const TrackProgram& track = sequence->program.tracks.front();
  expect(track.commands.size() == 7, "KonamiSnes fixture should decode tempo, setup, note, and end commands");
  constexpr std::array<std::string_view, 7> expectedCommandKinds{
      "konami-snes.tempo",   "konami-snes.program", "konami-snes.volume", "konami-snes.pan",
      "konami-snes.vibrato", "konami-snes.note",    "konami-snes.end"};
  for (size_t index = 0; index < expectedCommandKinds.size(); ++index) {
    expect(commandKind(project.sourceMap(), track.commands[index]) == expectedCommandKinds[index],
           "track should decode KonamiSnes command " + std::to_string(index));
  }

  expect(sequence->program.runtime.valid(), "scanned KonamiSnes sequence should retain its runtime");
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program);
  expect(performance.diagnostics.empty(), "KonamiSnes performance render should not report diagnostics");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 6,
         "KonamiSnes note duration should use the decoded duration rate");

  const auto vibratoDepth = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    return modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoDepth &&
           modulation->pitchDepthSemitones && *modulation->pitchDepthSemitones > 0.0;
  });
  expect(vibratoDepth != performance.tracks[0].events.end(),
         "KonamiSnes vibrato command should emit target-neutral depth");
  const auto& vibratoDepthEvent = std::get<ModulationPerformanceEvent>(*vibratoDepth);
  const double expectedDepthCents = vibrato::currentDepthCents(KONAMISNES_V6, 0x10, 0x10 << 8);
  expect(vibratoDepthEvent.amount == 0.0, "KonamiSnes vibrato depth should not contain destination controller scaling");
  expect(vibratoDepthEvent.pitchDepthSemitones &&
             std::abs(*vibratoDepthEvent.pitchDepthSemitones - (expectedDepthCents / 100.0)) < 0.0001,
         "KonamiSnes vibrato depth should retain peak pitch swing for sequence-event simulation");
  const auto vibratoDelay = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<VibratoDelayPerformanceEvent>(event);
  });
  expect(vibratoDelay != performance.tracks[0].events.end(),
         "KonamiSnes vibrato command should emit target-neutral delay");
  expect(std::get<VibratoDelayPerformanceEvent>(*vibratoDelay).delayTicks == 2,
         "KonamiSnes vibrato delay should be converted to rendered sequence ticks");

  const SequenceModulationProfile modulationProfile = analyzeSequenceModulation(performance);
  const MidiSequence synthModulationMidi =
      renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, {}, &modulationProfile);
  expect(hasMidiEvent<VibratoDepth>(synthModulationMidi.tracks[0]) &&
             hasMidiEvent<VibratoFrequency>(synthModulationMidi.tracks[0]) &&
             hasMidiEvent<VibratoDelay>(synthModulationMidi.tracks[0]),
         "default KonamiSnes MIDI rendering should preserve synth modulation controllers");

  const MidiSequence simulatedMidi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  expect(!hasMidiEvent<VibratoDepth>(simulatedMidi.tracks[0]) &&
             !hasMidiEvent<VibratoFrequency>(simulatedMidi.tracks[0]) &&
             !hasMidiEvent<VibratoDelay>(simulatedMidi.tracks[0]),
         "sequence-event modulation policy should suppress synth modulation controllers");
  expect(!hasNonZeroPitchBendBefore(simulatedMidi.tracks[0], 2),
         "sequence-event modulation policy should keep simulated vibrato silent before delay");
  expect(hasNonZeroPitchBendAtOrAfter(simulatedMidi.tracks[0], 2),
         "sequence-event modulation policy should emit nonzero vibrato pitch bend after delay");

  const auto* instruments = std::get_if<SoundBankAsset>(&project.assets()[1]);
  expect(instruments != nullptr, "second KonamiSnes asset should be an instrument set");
  expect(instruments->instruments.size() == 1, "instrument set should parse one valid melodic instrument");
  const Instrument& instrument = instruments->instruments.front();
  expect(instrument.explicitAddress == InstrumentAddress{.bank = 0, .program = 0},
         "instrument should preserve its explicit export address");
  expect(instrument.range.offset == 0x4000 && instrument.range.size == 7,
         "instrument should preserve its source header range");
  expect(instrument.regions.size() == 1, "instrument should contain one sample-backed region");
  expect(!instrument.modulation.vibrato,
         "scanned KonamiSnes instruments should not carry sequence-independent modulation guesses");
  SoundBankAsset preparedInstruments = *instruments;
  applySequenceModulation(preparedInstruments, modulationProfile);
  expect(preparedInstruments.instruments.front().modulation.vibrato &&
             preparedInstruments.instruments.front().modulation.vibrato->maxDepthCents > 0.0 &&
             preparedInstruments.instruments.front().modulation.vibrato->delaySeconds,
         "shared collection planning should add the sequence's physical vibrato range");

  const SamplePool& samples = instruments->localSamples;
  expect(samples.samples.size() == 1, "sound bank should parse one referenced BRR sample");
  expect(samples.samples.front().encodedData.offset == 0x6000 && samples.samples.front().encodedData.size == 9,
         "sample should preserve the one-block BRR payload range");
  expect(!samples.samples.front().loop.enabled && samples.samples.front().loop.start == 0 &&
             samples.samples.front().loop.length == 0,
         "non-looping KonamiSnes BRR samples should keep a zero loop span");

  const SourceMap& sourceMap = project.sourceMap();
  const auto* sequenceHeader = annotationWithKind(sourceMap, source, SourceRole::Header, "konami-snes-sequence-header");
  expect(sequenceHeader != nullptr && sequenceHeader->range.offset == 0x2000 && sequenceHeader->range.size == 2,
         "KonamiSnes scan should annotate the sequence header");
  const auto* trackPointer = annotationWithKind(sourceMap, source, SourceRole::Pointer, "konami-snes-track-pointer");
  expect(trackPointer != nullptr && trackPointer->range.offset == 0x2000 && trackPointer->range.size == 2,
         "KonamiSnes scan should annotate track pointers");
  const auto* instrumentTable =
      annotationWithKind(sourceMap, source, SourceRole::Table, "konami-snes-instrument-tables");
  expect(instrumentTable != nullptr && instrumentTable->range.offset == 0x4000 && instrumentTable->range.size == 7,
         "KonamiSnes scan should annotate instrument tables");
  const auto* instrumentRow = annotationWithKind(sourceMap, source, SourceRole::Instrument, "konami-snes-instrument");
  expect(instrumentRow != nullptr && instrumentRow->range.offset == 0x4000 && instrumentRow->range.size == 7,
         "KonamiSnes scan should annotate parsed instrument rows");
  const auto instrumentSampleLink = std::ranges::find_if(
      instrumentRow->links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesSample; });
  expect(instrumentSampleLink != instrumentRow->links.end(),
         "KonamiSnes instrument annotation should link to the referenced sample");
  const auto* regionAnnotation = annotationWithKind(sourceMap, source, SourceRole::Region, "konami-snes-region");
  expect(regionAnnotation != nullptr && regionAnnotation->owner == ObjectRefs::region(instruments->metadata.id, 0, 0),
         "KonamiSnes region annotations should identify their durable instrument and region indexes");
  const auto* sampleDir = annotationWithKind(sourceMap, source, SourceRole::Table, "snes-sample-dir");
  expect(sampleDir != nullptr && sampleDir->range.offset == 0x5000 && sampleDir->range.size == 4,
         "KonamiSnes scan should annotate the sample DIR table");
  const auto* sampleEntry = annotationWithKind(sourceMap, source, SourceRole::Sample, "snes-sample-dir-entry");
  expect(sampleEntry != nullptr && sampleEntry->range.offset == 0x5000 && sampleEntry->range.size == 4,
         "KonamiSnes scan should annotate sample DIR entries");
  const auto* samplePayload = annotationWithKind(sourceMap, source, SourceRole::Payload, "snes-brr-payload");
  expect(samplePayload != nullptr && samplePayload->range.offset == 0x6000 && samplePayload->range.size == 9,
         "KonamiSnes scan should annotate BRR payloads");
}

void konamiSnesSynthParsersStopAtInvalidBankedInstrument() {
  const auto bytes = makeKonamiSnesAram();
  const auto layout = findKonamiSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "KonamiSnes fixture should expose a layout for synth parser tests");
  const auto instruments = parseKonamiSnesInstrumentInfos(ByteReader(SourceId{8}, bytes), *layout);
  expect(instruments.size() == 1, "KonamiSnes parser should stop at the first invalid banked instrument");
  expect(instruments.front().index == 0 && instruments.front().source.range.offset == 0x4000,
         "KonamiSnes parser should preserve the sparse source instrument index and address");
  const auto samples = parseKonamiSnesSampleInfos(ByteReader(SourceId{8}, bytes), *layout->spcDirAddress, instruments);
  expect(samples.samples.size() == 1 && samples.samples.front().srcn == 0 &&
             samples.samples.front().stream.encodedData.size == 9,
         "KonamiSnes sample parser should keep only samples used by valid instruments");

  auto staleLoopBytes = bytes;
  writeLe16(staleLoopBytes, 0x5002, 0x1000);
  expect(readSnesSampleDirectoryEntry(ByteReader(SourceId{81}, staleLoopBytes), 0x5000, true).has_value(),
         "a stale loop pointer should not reject a non-looping BRR stream");
  staleLoopBytes[0x6000] = 0x03;
  expect(!readSnesSampleDirectoryEntry(ByteReader(SourceId{82}, staleLoopBytes), 0x5000, true),
         "a looping BRR stream should still require an aligned in-range loop pointer");

  auto highVolumeBytes = makeKonamiSnesBuilderAram();
  highVolumeBytes[0x4206] = 0x80;
  writeKonamiInstrumentEntry(highVolumeBytes, 0x4207, 4);
  highVolumeBytes[0x420e] = 0xff;
  const KonamiSnesLayout highVolumeLayout{
      .version = KONAMISNES_V6,
      .spcDirAddress = 0x5000,
      .commonInstrumentTableAddress = 0x4000,
      .bankedInstrumentTableAddress = 0x4200,
      .firstBankedInstrument = 5,
      .percussionInstrumentTableAddress = 0x4300,
  };
  const auto highVolumeInstruments =
      parseKonamiSnesInstrumentInfos(ByteReader(SourceId{83}, highVolumeBytes), highVolumeLayout);
  const auto programFive = std::ranges::find(highVolumeInstruments, 5, &KonamiSnesInstrumentInfo::index);
  const auto programSix = std::ranges::find(highVolumeInstruments, 6, &KonamiSnesInstrumentInfo::index);
  expect(programFive != highVolumeInstruments.end() && programFive->volume == 0x80 &&
             programSix != highVolumeInstruments.end(),
         "an unrestricted subtractive volume byte should not terminate a packed melodic bank");
}

void konamiSnesSynthBuilderGroupsPercussionAndPreservesSampleRules() {
  SourceStore sources;
  auto bytes = makeKonamiSnesBuilderAram();
  bytes[0x4003] = 0x00;
  bytes[0x4004] = 0x9f;
  const SourceId source = sources.add(SourceFile{.name = "konami-builder.spc"}, std::move(bytes));
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "KonamiSnes");
  const KonamiSnesLayout layout{
      .version = KONAMISNES_V6,
      .spcDirAddress = 0x5000,
      .commonInstrumentTableAddress = 0x4000,
      .bankedInstrumentTableAddress = 0x4200,
      .firstBankedInstrument = 5,
      .percussionInstrumentTableAddress = 0x4300,
  };
  const auto instrumentInfos = parseKonamiSnesInstrumentInfos(result.reader(), layout);
  const auto synth = addKonamiSnesSynth(result, layout, instrumentInfos, "Builder Probe");
  expect(synth.has_value(), "KonamiSnes builder fixture should produce a complete synth");
  const ScanResult scan = result.finish();

  const auto* instruments = std::get_if<SoundBankAsset>(&scan.assets[0]);
  expect(
      instruments != nullptr && instruments->instruments.size() == 4 && instruments->localSamples.samples.size() == 5,
      "KonamiSnes builder should retain three melodic programs, one grouped kit, and every source sample");
  expect(instruments->instruments[0].regions[0].sample.index() == 2,
         "KonamiSnes instruments should resolve samples directly by their SRCN");
  expect(instruments->instruments[1].regions[0].sample.index() == 0,
         "two SRCNs that name one BRR stream should resolve to the same canonical sample");
  expect(instruments->instruments[2].regions[0].sample.index() == 3,
         "ordinary Konami sample lookup should retain the SRCN's concrete sample reference");
  expect(instruments->instruments[0].regions[0].envelope == snesDspEnvelope(0x00, 0x9f, 0x9f) &&
             instruments->instruments[0].regions[0].attenuationDb == 0.0,
         "GAIN instruments should retain their DSP envelope while the subtractive volume byte stays runtime state");

  const Instrument& percussion = instruments->instruments[3];
  expect(percussion.explicitAddress == InstrumentAddress{.bank = 127, .program = 0} && percussion.regions.size() == 3,
         "percussion source entries should form one drum kit through getOrAdd");
  expect(percussion.regions[0].sample.index() == 0 && percussion.regions[1].sample.index() == 3 &&
             percussion.regions[2].sample.index() == 4,
         "percussion regions should retain their direct source sample references");

  const auto sparseSources = scan.sourceMap.ownedBy(ObjectRefs::instrument(synth->id(), 1));
  expect(sparseSources.size() == 1 && scan.sourceMap.get(sparseSources[0]).range.offset == 0x401c,
         "sparse source program 4 should use dense instrument owner 1");
  expect(scan.sourceMap.ownedBy(ObjectRefs::instrument(synth->id(), 4)).empty(),
         "a sparse source program must not be mistaken for a dense annotation owner");

  const auto percussionSources = scan.sourceMap.ownedBy(ObjectRefs::instrument(synth->id(), 3));
  expect(percussionSources.size() == 3, "every percussion source entry should point back to the one durable drum kit");
  for (const SourceAnnotationId id : percussionSources) {
    const SourceAnnotation& annotation = scan.sourceMap.get(id);
    const auto sampleLinks = std::ranges::count_if(
        annotation.links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesSample; });
    expect(sampleLinks == 3,
           "each drum-kit source record should expose the kit's complete, deduplicated sample relationship");
  }
  for (u32 regionIndex = 0; regionIndex < percussion.regions.size(); ++regionIndex) {
    expect(scan.sourceMap.ownedBy(ObjectRefs::region(synth->id(), 3, regionIndex)).size() == 1,
           "each grouped percussion region should retain its own stable source owner");
  }
}

void konamiSnesProgramChangeReemitsCurrentFineTune() {
  constexpr std::array<u8, 15> bytes{
      0xf2, 0xf4,              // tune down
      0x3c, 0x05, 0x7f, 0x7f,  // establish the active fine tune before the switch
      0xe2, 0x09,              // switch to program 9 while tuning is still active
      0x3c, 0x05, 0x7f, 0x7f,  // note at the same tick
      0xff,
  };

  const PerformanceSequence performance = renderKonamiSnesTrack(bytes);
  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks[0].events;

  const auto programChange = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* program = std::get_if<ProgramChange>(&event);
    return program != nullptr && program->tick == 5 && program->program == 9;
  });
  expect(programChange != events.end(), "KonamiSnes program change should render at the expected tick");

  const auto sameTickFineTune = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* fineTune = std::get_if<FineTune>(&event);
    return fineTune != nullptr && fineTune->tick == 5 && std::abs(fineTune->cents + 18.75) < 0.001;
  });
  expect(sameTickFineTune != events.end(),
         "KonamiSnes should re-emit the active fine tune before same-tick program/note playback");
  expect(std::distance(events.begin(), sameTickFineTune) < std::distance(events.begin(), programChange),
         "KonamiSnes fine tune should be ordered before the same-tick program change");
}

void konamiSnesEarlyVibratoQuantizesRateAtCommandTempo() {
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V2, {
                                                 {
                                                     0xea,
                                                     0x37,  // tempo 55
                                                     0xe4,
                                                     0x00,
                                                     0x2d,
                                                     0x63,  // active legacy vibrato, rate step 45
                                                     0xff,
                                                 },
                                                 {
                                                     0xea,
                                                     0x78,  // another track's tempo must not rewrite the stored step
                                                     0xff,
                                                 },
                                             });

  const auto vibratoRate = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    return modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoRate &&
           modulation->context.frequencyHz && *modulation->context.frequencyHz > 0.0;
  });
  expect(vibratoRate != performance.tracks[0].events.end(),
         "KonamiSnes legacy vibrato should emit a rate modulation event");

  const double baseHz = vibrato::baseHz(KONAMISNES_V2);
  const SequenceModulationProfile profile = analyzeSequenceModulation(performance);
  const u16 expectedFactor = static_cast<u16>(vibrato::foldedPhaseStep(vibrato::earlyPhaseStep(0x2d, 0x37))) << 8;
  expect(profile.instruments.vibrato &&
             std::abs(profile.instruments.vibrato->rateHertz.minimum - baseHz * expectedFactor) < 0.0001 &&
             std::abs(profile.instruments.vibrato->rateHertz.maximum - baseHz * expectedFactor) < 0.0001,
         "legacy KonamiSnes vibrato should retain the phase step quantized when its command executes");

  const PerformanceSequence ordered =
      renderKonamiSnesProgram(KONAMISNES_V2, {
                                                 {
                                                     0xe0,
                                                     0x04,  // wait
                                                     0xea,
                                                     0x78,  // global tempo change at tick four
                                                     0xff,
                                                 },
                                                 {
                                                     0xe4,
                                                     0x00,
                                                     0x20,
                                                     0x40,  // initial vibrato
                                                     0xe0,
                                                     0x04,
                                                     0xe4,
                                                     0x00,
                                                     0x10,
                                                     0x40,  // later same-tick replacement
                                                     0xff,
                                                 },
                                             });
  std::vector<const ModulationPerformanceEvent*> sameTickRates;
  for (const PerformanceEvent& event : ordered.tracks[1].events) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    if (modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoRate &&
        modulation->header.tick == 4) {
      sameTickRates.push_back(modulation);
    }
  }
  expect(sameTickRates.size() == 1 && sameTickRates[0]->context.frequencyHz && !sameTickRates[0]->context.cyclesPerTick,
         "an unrelated tempo event should not synthesize a replacement for an early driver's stored vibrato step");

  const PerformanceSequence direction =
      renderKonamiSnesProgram(KONAMISNES_V2, {{
                                                 0xea,
                                                 0x80,
                                                 0xe4,
                                                 0x00,
                                                 0xff,
                                                 0x40,  // floor($ff * $80 / 256) = $7f: fast, forward
                                                 0xe0,
                                                 0x01,
                                                 0xea,
                                                 0xff,
                                                 0xe4,
                                                 0x00,
                                                 0xff,
                                                 0x40,  // floor($ff * $ff / 256) = $fe: slow, backward
                                                 0xff,
                                             }});
  std::vector<const ModulationPerformanceEvent*> directionRates;
  for (const PerformanceEvent& event : direction.tracks[0].events) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    if (modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoRate) {
      directionRates.push_back(modulation);
    }
  }
  expect(
      directionRates.size() == 2 && directionRates[0]->context.frequencyHz && directionRates[1]->context.frequencyHz &&
          std::abs(*directionRates[0]->context.frequencyHz - (250.0 * 127.0 / 256.0)) < 0.0001 &&
          std::abs(*directionRates[1]->context.frequencyHz - (250.0 * 2.0 / 256.0)) < 0.0001 &&
          directionRates[0]->context.initialPhaseCycles == 0.0 && directionRates[1]->context.initialPhaseCycles == 0.5,
      "early KonamiSnes high rates should fold only after tempo multiplication and preserve triangle direction");
}

void konamiSnesEchoPreservesGlobalDspState() {
  const PerformanceSequence performance = renderKonamiSnesProgram(
      KONAMISNES_V6,
      {{0xf4, 0x01, 0x40, 0xc0, 0xf5, 0x04, 0xe0, 0xaa, 0xe0, 0x02, 0xf4, 0, 0x7f, 0x7f, 0xff}, {0xe0, 0x04, 0xff}});
  const auto changes = performanceEvents<ReverbPerformanceEvent>(performance.tracks[0]);
  expect(changes.size() == 4 && changes[1]->voiceMask == 1 && std::abs(*changes[1]->leftGain - 64.0 / 127.0) < 0.0001 &&
             std::abs(*changes[1]->rightGain + 64.0 / 127.0) < 0.0001,
         "Konami F4 should preserve signed stereo EVOL and the global EON mask");
  expect(changes[2]->delayMilliseconds == 64.0 && std::abs(*changes[2]->feedback + 0.25) < 0.0001 &&
             changes[2]->filterIndex == 2 && changes[3]->voiceMask == 0,
         "Konami F5 should preserve fixed DSP echo state and a zero F4 mask should disable it");

  const PerformanceSequence indexed =
      renderKonamiSnesProgram(KONAMISNES_V2, {{0xf5, 0x02, 0x10, 0x01, 0xf4, 0x01, 0x20, 0x20, 0xff}}, 0,
                              /*indexedEchoFilter=*/true);
  const auto indexedChanges = performanceEvents<ReverbPerformanceEvent>(indexed.tracks[0]);
  expect(indexedChanges.size() == 2 && indexedChanges.back()->filterIndex == 1,
         "Konami F5 should retain indexed FIR state while echo is disabled");
}

void konamiSnesLinearDriverPitchUsesSharedTransitions() {
  struct Case {
    KonamiSnesVersion version;
    std::vector<u8> bytes;
    u64 startTick;
    double startKey;
    double targetKey;
    u32 timelineTicks;
  };
  const std::array cases{
      Case{KONAMISNES_V1, {0xea, 0x4e, 0x3c, 2, 0x7f, 0x7f, 0xf0, 5, 0x40, 6, 0x7f, 0x7f, 0xff}, 2, 60, 64, 5},
      Case{KONAMISNES_V2, {0x3c, 2, 0x7f, 0x7f, 0xf0, 4, 0x40, 6, 0x7f, 0x7f, 0xff}, 2, 60, 64, 4},
      Case{KONAMISNES_V2, {0xf1, 2, 4, 2, 0x3c, 8, 0x7f, 0x7f, 0xff}, 2, 58, 60, 4},
      Case{KONAMISNES_V6, {0xf1, 1, 3, 2, 0x80, 0, 0x3c, 8, 0x7f, 0x7f, 0xff}, 1, 58, 60, 3},
  };
  for (const auto& test : cases) {
    const auto performance = renderKonamiSnesProgram(test.version, {test.bytes});
    const auto transition =
        std::ranges::find_if(performance.tracks[0].automations, [&](const PerformanceAutomation& automation) {
          const auto* intent = pitchTransitionIntent(automation);
          return intent != nullptr && automation.realization.startTick == test.startTick &&
                 intent->startKey == test.startKey && intent->targetKey == test.targetKey &&
                 intent->timing.timelineTicks == test.timelineTicks;
        });
    expect(transition != performance.tracks[0].automations.end(),
           "Konami pitch should declare the expected shared transition");
    const auto* intent = pitchTransitionIntent(*transition);
    expect(intent != nullptr && std::holds_alternative<TempoRelativePitchSlideTiming>(intent->timing.physical),
           "Konami F0/F1 counters should retain their tempo-relative sequence timing");
  }
}

void konamiSnesProportionalPortamentoMatchesDriverCurve() {
  // Pop 'n' TwinBee, Big Airship, track 4 at ARAM $350b. From V3 onward F0 is
  // a 250 Hz proportional rate: $9b moves 155/256 of the remaining distance.
  const PerformanceSequence proportional =
      renderKonamiSnesProgram(KONAMISNES_V3, {{0xea, 0x82,           // track tempo from the song
                                               0x28, 1, 0x7d, 0x7d,  // establish key $28
                                               0xf0, 0x9b,           // persistent proportional portamento
                                               0x2f, 8, 0x7d, 0x7d,  // glide seven semitones upward
                                               0xff}});
  expect(proportional.tracks[0].automations.size() == 1,
         "V3 proportional portamento should create a transition for every following note");
  const auto* proportionalIntent = pitchTransitionIntent(proportional.tracks[0].automations.front());
  expect(proportionalIntent != nullptr, "V3 proportional portamento should use shared pitch-transition intent");
  const auto* physicalDuration = std::get_if<FixedDurationPitchSlideTiming>(&proportionalIntent->timing.physical);
  const auto* sampledCurve = std::get_if<SampledAutomationCurve>(&proportionalIntent->curve);
  expect(physicalDuration != nullptr && physicalDuration->milliseconds == 40.0 &&
             proportionalIntent->timing.timelineTicks == 3 && sampledCurve != nullptr &&
             sampledCurve->samples.size() == 4 && sampledCurve->samples[1].value == 46.56640625 &&
             sampledCurve->samples[2].value == 46.984375,
         "F0 $9b should preserve the driver's fast, front-loaded integer pitch curve");

  const MidiSequence pitchBend =
      renderMidiSequence(proportional, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const MidiSequence portamento =
      renderMidiSequence(proportional, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(hasMidiEvent<PitchBend>(pitchBend.tracks[0]) && hasMidiEvent<PortamentoControl>(portamento.tracks[0]),
         "V3 proportional portamento should render in both MIDI transition modes");

  const PerformanceSequence interrupted = renderKonamiSnesProgram(
      KONAMISNES_V3, {{0x28, 1, 0x7d, 0x7d, 0xf0, 1, 0x2f, 1, 0x7d, 0x7d, 0x30, 8, 0x7d, 0x7d, 0xff}});
  expect(interrupted.tracks[0].automations.size() == 2, "each note should restart persistent proportional portamento");
  const auto* retargeted = pitchTransitionIntent(interrupted.tracks[0].automations.back());
  expect(retargeted != nullptr && retargeted->targetKey == 48.0 && retargeted->startKey < 41.0,
         "a new note should continue from an interrupted proportional glide, not its old target");
}

void konamiSnesPercussionUsesPackedGsDrumBank() {
  constexpr std::array<u8, 10> bytes{
      0x60,  // percussion on
      0x04, 0x06, 0x7f, 0x7d,
      0x61,              // percussion off only clears the source-mode flag
      0xe1, 0x18, 0x7d,  // tie the preceding drum note
      0xff,
  };

  const PerformanceSequence performance = renderKonamiSnesTrack(bytes);
  expect(performanceEvents<InstrumentPerformanceEvent>(performance.tracks.front()).size() == 1,
         "percussion off should not invent a melodic instrument restoration");
  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks[0].events;

  const auto drumBank = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* bank = std::get_if<BankSelect>(&event);
    return bank != nullptr && bank->tick == 0;
  });
  expect(drumBank != events.end(), "KonamiSnes percussion should emit a drum bank select");
  expect(std::get<BankSelect>(*drumBank).bank == (0x7f << 7),
         "KonamiSnes percussion should use the packed GS bank field so MIDI serializes bank MSB 127");
  const auto midiNote = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* note = std::get_if<NoteDuration>(&event);
    return note != nullptr && note->key == 4;
  });
  expect(midiNote != events.end() && std::get<NoteDuration>(*midiNote).duration == 29,
         "percussion off should not prevent a tie from extending the preceding drum note");
}

void konamiSnesCompilerCursorDecodesVersionedFlowAndTruncation() {
  const std::vector<u8> flowBytes{
      0xfc, 0x08, 0x00, 0x0c, 0x00,        // V1 jump plus alternate discovery target
      0xff, 0x00, 0x00, 0xfe, 0x0c, 0x00,  // call alternate target
      0xff,                                // return from the call
      0xff,                                // alternate target
  };
  SourceMapBuilder flowSourceMapBuilder;
  const TrackProgram flow =
      decodeKonamiSnesSourceTrack(ByteReader(SourceId{30}, flowBytes), KONAMISNES_V1, 0, 0, &flowSourceMapBuilder);
  const SourceMap flowSourceMap = flowSourceMapBuilder.finish();
  const auto conditionalIndex = flow.commandIndex(Address{0});
  const auto callIndex = flow.commandIndex(Address{8});
  expect(conditionalIndex && callIndex, "Konami compiler decoding should retain reachable branch and call blocks");
  const SourceCommand& conditional = flow.commands[*conditionalIndex];
  const SourceCommand& call = flow.commands[*callIndex];
  const SourceAnnotation& conditionalAnnotation = commandAnnotation(flowSourceMap, conditional);
  const bool hasAlternateTarget = std::ranges::any_of(conditionalAnnotation.links, [](const SourceLink& link) {
    const auto* range = std::get_if<SourceRange>(&link.target);
    return link.role == SourceLinkRole::JumpTarget && range != nullptr && range->offset == 12;
  });
  expect(
      conditional.flow.defaultDestination() && conditional.flow.defaultDestination()->value == 8 && hasAlternateTarget,
      "Konami conditional jump should expose both decoded branch targets");
  expect(call.flow.callTarget() && call.flow.defaultDestination()->value == 12,
         "Konami call should expose its decoded little-endian target");
  expect(std::ranges::all_of(flow.commands, [](const SourceCommand& command) { return command.range.size != 0; }),
         "valid Konami compiler commands should retain semantic IR and source ranges");

  const std::vector<u8> truncatedBytes{0xe4, 0x01, 0x20};
  std::vector<Diagnostic> diagnostics;
  const TrackProgram truncated =
      decodeKonamiSnesSourceTrack(ByteReader(SourceId{31}, truncatedBytes), KONAMISNES_V6, 0, 0, nullptr, &diagnostics);
  expect(truncated.commands.size() == 1 && truncated.commands[0].flow.endsPlayback() &&
             !truncated.commands[0].execution.valid() && truncated.commands[0].range.size == 3,
         "truncated Konami commands should keep their partial source range but no executable behavior");
  expect(!diagnostics.empty() && diagnostics.front().code == "truncated-record",
         "truncated Konami operands should use the shared compiler-cursor diagnostic");
}

void konamiSnesCompilerCursorUsesVersionedOperandLengths() {
  const auto firstSize = [](KonamiSnesVersion version, std::vector<u8> bytes) {
    const TrackProgram track = decodeKonamiSnesSourceTrack(ByteReader(SourceId{32}, bytes), version, 0, 0);
    return track.commands.front().range.size;
  };

  expect(firstSize(KONAMISNES_V1, {0xf3, 0x00, 0x02, 0x40, 0xff}) == 4,
         "V1 pitch slide should use its four-byte command layout");
  expect(firstSize(KONAMISNES_V2, {0xf3, 0x00, 0x00, 0x40, 0xff}) == 4,
         "zero-length V2 pitch slide should omit reserved and delta operands");
  expect(firstSize(KONAMISNES_V2, {0xf3, 0x00, 0x02, 0x40, 0x00, 0x34, 0x12, 0xff}) == 7,
         "active V2 pitch slide should include reserved and delta operands");
  expect(firstSize(KONAMISNES_V6, {0xf3, 0x00, 0x02, 0x40, 0x34, 0x12, 0xff}) == 6,
         "late pitch slide should use its six-byte command layout");
  expect(firstSize(KONAMISNES_V1, {0x63, 0xaa, 0xff}) == 2 && firstSize(KONAMISNES_V1, {0x64, 0xaa, 0xbb, 0xff}) == 3 &&
             firstSize(KONAMISNES_V6, {0x63, 0xff}) == 1,
         "V1 defaults and V6 EON controls should retain their version-dependent operand lengths");
  expect(firstSize(KONAMISNES_V4, {0xfb, 0x34, 0x12, 0xff}) == 3 &&
             firstSize(KONAMISNES_V4, {0xfc, 0x78, 0x56, 0xff}) == 3,
         "V4 pitch-envelope aliases should retain both delta bytes");
  expect(firstSize(KONAMISNES_V4, {0x62, 0xaa, 0xff}) == 1,
         "V4 opcode 0x62 should terminate without consuming a release operand");
  expect(firstSize(KONAMISNES_V2, {0x62, 0xff}) == 1 && firstSize(KONAMISNES_V3, {0x62, 0xff}) == 1 &&
             firstSize(KONAMISNES_V5, {0x62, 0x64, 0xff}) == 2,
         "opcode 0x62 should consume a release byte only in V5-V6");
  expect(firstSize(KONAMISNES_V4, {0xed, 1, 2, 3, 0xff}) == 4 && firstSize(KONAMISNES_V6, {0xed, 0x8f, 0xff}) == 2 &&
             firstSize(KONAMISNES_V1, {0xfa, 1, 2, 3, 0xff}) == 4 &&
             firstSize(KONAMISNES_V6, {0xfa, 1, 2, 3, 0xff}) == 4,
         "ED and FA should retain their generation-specific operand counts");
}

void konamiSnesDynamicAdsrMatchesEachDriverFamily() {
  const auto envelopesFor = [](const PerformanceSequence& performance) {
    std::vector<EnvelopePerformanceEvent> result;
    for (const auto* event : performanceEvents<EnvelopePerformanceEvent>(performance.tracks.front())) {
      result.push_back(*event);
    }
    return result;
  };

  const auto v1 = envelopesFor(renderKonamiSnesProgram(KONAMISNES_V1, {{0xfa, 35, 64, 16, 0xe2, 0x01, 0xff}}));
  Envelope expectedV1 = snesDspEnvelope(0xd3, 0x46, 0x46);
  expect(v1.size() == 1 && v1[0].update.values == expectedV1 && v1[0].update.fields == EnvelopeFields::All,
         "V1 0xFA should decode decimal ADSR parameters without turning its software release into a synth envelope");

  const auto contraPerformance = renderKonamiSnesProgram(
      KONAMISNES_V1, {{0xea, 0x4e, 0xee, 0x7f, 0xfa, 0x8c, 0xd2, 0x64, 0x3c, 0x08, 0x32, 0x8a, 0xff}});
  const auto contra = envelopesFor(contraPerformance);
  Envelope expectedContra = snesDspEnvelope(0x8e, 0xe2, 0xe2);
  const auto contraNotes = performanceEvents<NotePerformanceEvent>(contraPerformance.tracks.front());
  const auto contraLevels = performanceEvents<LevelPerformanceEvent>(contraPerformance.tracks.front());
  expect(contra.size() == 1 && contra[0].update.values == expectedContra && contraNotes.size() == 1 &&
             contraNotes[0]->durationTicks == 6 && contraLevels.size() > 3 && contraLevels.back()->linearGain == 0.0,
         "V1 0xFA software release should extend the keyed voice and fade the driver's mixed level");

  const auto v1Gain = envelopesFor(renderKonamiSnesProgram(KONAMISNES_V1, {{0xfa, 0xa0, 0x9f, 0x00, 0xff}}));
  expect(v1Gain.size() == 1 && v1Gain[0].update.values == snesDspEnvelope(0x00, 0x9f, 0x9f),
         "V1 0xFA should treat attack/decay values 0xA0 and above as direct GAIN mode");

  const auto v3 = envelopesFor(renderKonamiSnesProgram(KONAMISNES_V3, {{0xfa, 35, 64, 115, 0xff}}));
  Envelope expectedV3 = snesDspEnvelope(0xd3, 0x46, 0x46);
  expectedV3.releaseSeconds = snesDspGainEnvelopeSeconds(0x8f, 0x7ff, 0);
  expect(v3.size() == 1 && v3[0].update.values == expectedV3,
         "V3 0xFA should switch release values 100 and above to DSP GAIN");

  const auto v4 = envelopesFor(renderKonamiSnesProgram(KONAMISNES_V4, {{0xfa, 0x8f, 0xe0, 115, 0x62, 0x00, 0xff}}));
  expect(v4.size() == 1 && v4[0].update.values && v4[0].update.values->releaseSeconds == expectedV3.releaseSeconds,
         "V4 0x62 should stop after the raw 0xFA command rather than consume another byte");

  const auto renderV6Instrument = [](u8 instrumentAdsr1) {
    auto bytes = makeKonamiSnesBuilderAram();
    bytes[0x401c + 3] = instrumentAdsr1;
    writeLe16(bytes, 0x2000, 0x2002);
    writeBytes(bytes, 0x2002, std::array<u8, 7>{0xe2, 0x04, 0xed, 0x8a, 0xfb, 0x42, 0xff});
    const KonamiSnesLayout layout{
        .version = KONAMISNES_V6,
        .sequenceHeaderAddress = 0x2000,
        .spcDirAddress = 0x5000,
        .commonInstrumentTableAddress = 0x4000,
        .bankedInstrumentTableAddress = 0x4200,
        .firstBankedInstrument = 5,
        .percussionInstrumentTableAddress = 0x4300,
    };
    const ByteReader reader(SourceId{33}, bytes);
    const SequenceProgram program =
        decodeKonamiSnesSequence(reader, layout, AssetId{33}, parseKonamiSnesInstrumentInfos(reader, layout));
    return SequenceVm(LoopPolicy::PlayOnce).render(program);
  };

  const auto v6 = envelopesFor(renderV6Instrument(0x8f));
  expect(v6.size() == 2 && v6[0].update.values == snesDspEnvelope(0x8a, 0xe0, 0xe0) &&
             v6[1].update.values == snesDspEnvelope(0x8a, 0x42, 0x42),
         "V5-V6 ADSR1 and ADSR2 commands should combine with the selected instrument's companion register");

  const auto gain = envelopesFor(renderV6Instrument(0x00));
  expect(gain.empty(), "V5-V6 should ignore standalone ADSR writes while the selected instrument uses GAIN");
}

void konamiSnesPreservesLateEnvelopeRegisterState() {
  const KonamiSnesLayout layout{
      .version = KONAMISNES_V6,
      .sequenceHeaderAddress = 0x2000,
      .spcDirAddress = 0x5000,
      .commonInstrumentTableAddress = 0x4000,
      .bankedInstrumentTableAddress = 0x4200,
      .firstBankedInstrument = 5,
      .percussionInstrumentTableAddress = 0x4300,
  };

  auto bytes = makeKonamiSnesBuilderAram();
  bytes[0x4003] = 0x00;
  bytes[0x4004] = 0x9f;
  writeLe16(bytes, 0x2000, 0x2002);
  writeBytes(bytes, 0x2002, std::array<u8, 11>{0xe2, 0x00, 0xfa, 0x8f, 0x42, 0x00, 0xfb, 0x55, 0xed, 0x00, 0xff});
  const auto dynamic = renderKonamiSnesAramSequence(bytes, layout);
  const auto updates = performanceEvents<EnvelopePerformanceEvent>(dynamic.tracks.front());
  expect(updates.size() == 3 && updates[0]->update.values == snesDspEnvelope(0x8f, 0x42, 0x9f) &&
             updates[1]->update.values == snesDspEnvelope(0x8f, 0x55, 0x9f) &&
             updates[2]->update.values == snesDspEnvelope(0x00, 0x55, 0x9f),
         "FA and FB should change active ADSR registers without destroying the independent physical GAIN value");

  bytes = makeKonamiSnesBuilderAram();
  bytes[0x4003] = 0x00;
  bytes[0x4004] = 0x9f;
  writeLe16(bytes, 0x2000, 0x2002);
  writeBytes(bytes, 0x2002, std::array<u8, 7>{0xe2, 0x00, 0xe2, 0x04, 0xed, 0x00, 0xff});
  const auto instrumentChange = renderKonamiSnesAramSequence(bytes, layout);
  const auto afterInstrument = performanceEvents<EnvelopePerformanceEvent>(instrumentChange.tracks.front());
  expect(afterInstrument.size() == 1 && afterInstrument.front()->update.values == snesDspEnvelope(0x00, 0xe0, 0x9f),
         "a late ADSR instrument load should preserve inactive GAIN even though it saves a new companion");

  writeBytes(bytes, 0x2002,
             std::array<u8, 17>{0xe2, 0x00, 0xe2, 0x04, 0xed, 0x00, 0x62, 115, 0x3c, 1, 0x40, 0x7f, 0x3d, 1, 0x40, 0x7f,
                                0xff});
  const auto released = renderKonamiSnesAramSequence(bytes, layout);
  const auto restored = performanceEvents<EnvelopePerformanceEvent>(released.tracks.front());
  Envelope expectedRestore = snesDspEnvelope(0x00, 0xe0, 0xe0);
  expectedRestore.releaseSeconds = snesDspGainEnvelopeSeconds(0x8f, 0x7ff, 0);
  expect(restored.size() == 3 && restored.back()->update.values == expectedRestore,
         "the note after DSP decrease-GAIN should restore the saved companion rather than the prior active GAIN");
}

void konamiSnesMixerAndPanFollowVersionedDriverMath() {
  expect(konamiSnesSequenceConfig(KONAMISNES_V1).behavior.initialLevel == 0.0,
         "Konami tracks should begin at the driver's zero volume");

  const auto lastLevel = [](const PerformanceSequence& performance) {
    const auto levels = performanceEvents<LevelPerformanceEvent>(performance.tracks.front());
    expect(!levels.empty(), "mixer fixture should emit a composite level");
    return levels.back()->linearGain;
  };
  const double v1 = lastLevel(renderKonamiSnesProgram(KONAMISNES_V1, {{0xee, 0x40, 0x3c, 1, 100, 0x40, 0xff}}));
  const double v2 = lastLevel(renderKonamiSnesProgram(KONAMISNES_V2, {{0xee, 0x40, 0x3c, 1, 0x7e, 0x40, 0xff}}));
  expect(std::abs(v1 - 32.0 / 127.0) < 0.0001 && std::abs(v2 - 1.0 / 127.0) < 0.0001,
         "V1 should divide note times track by 128 while later versions divide by 256 before the volume curve");

  const double v5 = lastLevel(renderKonamiSnesProgram(KONAMISNES_V5, {{0xee, 190, 0x3c, 1, 0x7f, 0x7f, 0xff}}));
  const double v6 = lastLevel(renderKonamiSnesProgram(KONAMISNES_V6, {{0xee, 190, 0x3c, 1, 0x7f, 0x7f, 0xff}}));
  expect(std::abs(v5 - 0x23 / 127.0) < 0.0001 && std::abs(v6 - 0x20 / 127.0) < 0.0001,
         "Goemon 3's anomalous volume-table byte should remain distinct from the corrected V6 byte");

  const auto v3PanPerformance = renderKonamiSnesProgram(KONAMISNES_V3, {{0xe3, 0, 0xff}});
  const auto v5PanPerformance = renderKonamiSnesProgram(KONAMISNES_V5, {{0xe3, 10, 0xff}});
  const auto v6PanPerformance = renderKonamiSnesProgram(KONAMISNES_V6, {{0xe3, 10, 0xff}});
  const auto v3Pan = performanceEvents<StereoBalancePerformanceEvent>(v3PanPerformance.tracks.front());
  const auto v5Pan = performanceEvents<StereoBalancePerformanceEvent>(v5PanPerformance.tracks.front());
  const auto v6Pan = performanceEvents<StereoBalancePerformanceEvent>(v6PanPerformance.tracks.front());
  expect(v3Pan.size() == 2 && v3Pan.back()->leftGain == 0.0 &&
             std::abs(v3Pan.back()->rightGain - 254.0 / 256.0) < 0.0001 && v5Pan.size() == 2 &&
             std::abs(v5Pan.back()->leftGain - 0x46 / 256.0) < 0.0001 && v6Pan.size() == 2 &&
             std::abs(v6Pan.back()->leftGain - 0x40 / 256.0) < 0.0001,
         "late pan should use left[pan], right[40-pan], a divisor of 256, and the V5 table anomaly");
}

void konamiSnesZeroNotesAndLegatoMatchDriverGating() {
  const std::vector<u8> zeroAfterNote{0xee, 0x80, 0x3c, 1, 0x7f, 0x7f, 0x3d, 1, 0x7f, 0x00, 0xff};
  const auto v2 = renderKonamiSnesProgram(KONAMISNES_V2, {zeroAfterNote});
  const auto v3 = renderKonamiSnesProgram(KONAMISNES_V3, {zeroAfterNote});
  const auto v2Levels = performanceEvents<LevelPerformanceEvent>(v2.tracks.front());
  const auto v3Levels = performanceEvents<LevelPerformanceEvent>(v3.tracks.front());
  expect(performanceEvents<NotePerformanceEvent>(v2.tracks.front()).size() == 1 &&
             performanceEvents<NotePerformanceEvent>(v3.tracks.front()).size() == 1 &&
             std::ranges::any_of(v2Levels,
                                 [](const LevelPerformanceEvent* level) {
                                   return level->header.tick == 1 && level->linearGain == 0.0;
                                 }) &&
             std::ranges::none_of(v3Levels, [](const LevelPerformanceEvent* level) { return level->header.tick == 1; }),
         "zero note volume should suppress attack, with only V1-V2 writing a zero composite level");

  const auto silentFade =
      renderKonamiSnesProgram(KONAMISNES_V3, {{0xee, 0x80, 0xef, 4, 0x40, 0xe0, 4, 0x3c, 1, 0x7f, 0x7f, 0xff}});
  const auto silentFadeLevels = performanceEvents<LevelPerformanceEvent>(silentFade.tracks.front());
  expect(silentFade.tracks.front().automations.empty() && !silentFadeLevels.empty() &&
             std::abs(silentFadeLevels.back()->linearGain - 2.0 / 127.0) < 0.0001,
         "a late volume fade should advance silently at zero note volume and apply its raw result to the next note");

  const auto v1Rate100 = renderKonamiSnesProgram(KONAMISNES_V1, {{0x62, 100, 0x3c, 4, 0xff, 0x3e, 4, 0xff, 0xff}});
  const auto v1Rate101 = renderKonamiSnesProgram(KONAMISNES_V1, {{0x62, 101, 0x3c, 4, 0xff, 0x3e, 4, 0xff, 0xff}});
  expect(v1Rate100.tracks.front().automations.empty() && v1Rate101.tracks.front().automations.size() == 1,
         "V1 duration 100 should gate for the full note but only 101 should continue a changing pitch");
  const auto continuedNotes = performanceEvents<NotePerformanceEvent>(v1Rate101.tracks.front());
  expect(continuedNotes.size() == 2 && continuedNotes[1]->restartsLfoPhase &&
             continuedNotes[1]->restartsVibratoLfoPhase == true,
         "a Konami legato source note should still reset its per-note LFO state");

  const auto tied = renderKonamiSnesProgram(KONAMISNES_V1, {{0x3c, 4, 100, 0x7f, 0x62, 1, 0xe1, 2, 50, 0xff}});
  const auto tiedNotes = performanceEvents<NotePerformanceEvent>(tied.tracks.front());
  expect(tiedNotes.size() == 2 && tiedNotes.back()->extendsPrevious,
         "E1 should test the preceding note's raw held rate even after the default duration changes");

  const auto afterRest = renderKonamiSnesProgram(KONAMISNES_V2, {{0x3c, 2, 0x7f, 0x7f, 0xe0, 1, 0xe1, 2, 0x7f, 0xff}});
  expect(performanceEvents<NotePerformanceEvent>(afterRest.tracks.front()).size() == 1,
         "an explicit tie after a rest should not revive the earlier held note");

  const auto portamentoAfterRest =
      renderKonamiSnesProgram(KONAMISNES_V1, {{0xfa, 0x93, 0xbb, 0x64,  // dynamic ADSR with software release
                                               0x3c, 4, 100, 0x7f,      // establish the portamento's prior pitch
                                               0xe0, 1,                 // rest forces the next note to attack
                                               0xf0, 3,                 // persistent portamento
                                               0x3e, 4, 100, 0x7f, 0xff}});
  const auto portamentoNotes = performanceEvents<NotePerformanceEvent>(portamentoAfterRest.tracks.front());
  const auto transition = std::ranges::find_if(
      portamentoAfterRest.tracks.front().automations,
      [](const PerformanceAutomation& automation) { return pitchTransitionIntent(automation) != nullptr; });
  expect(portamentoNotes.size() == 2 && portamentoNotes[0]->note != portamentoNotes[1]->note &&
             transition != portamentoAfterRest.tracks.front().automations.end() &&
             pitchTransitionIntent(*transition)->note == portamentoNotes[1]->note &&
             !pitchTransitionIntent(*transition)->previousNote,
         "V1 portamento after a rest should retain the prior pitch but attack a new voice");
}

void konamiSnesLowCommandsAndInstrumentPanAreVersioned() {
  const auto v1Default = renderKonamiSnesProgram(KONAMISNES_V1, {{0xee, 0x7f, 0x3c, 1, 100, 0x7f, 0x63, 0x00, 0xff}});
  const auto v1DefaultLevels = performanceEvents<LevelPerformanceEvent>(v1Default.tracks.front());
  expect(
      std::ranges::none_of(v1DefaultLevels, [](const LevelPerformanceEvent* level) { return level->header.tick == 1; }),
      "V1 opcode 0x63 should change only the saved default note volume");

  const auto tuningPerformance = renderKonamiSnesProgram(KONAMISNES_V5, {{0x78, 0xff}});
  const auto tuning = performanceEvents<TuningPerformanceEvent>(tuningPerformance.tracks.front());
  expect(tuning.size() == 1 && std::abs(tuning.front()->cents + 12.5) < 0.0001,
         "instant-tuning nibble 8 should decode as signed -8");

  const auto echoPerformance = renderKonamiSnesProgram(KONAMISNES_V6, {{0xff}, {0x63, 0xe0, 1, 0x64, 0xff}});
  const auto echo = performanceEvents<ReverbPerformanceEvent>(echoPerformance.tracks[1]);
  expect(echo.size() == 3 && echo[1]->voiceMask == 0x02 && echo[2]->voiceMask == 0,
         "V6 low opcodes 0x63 and 0x64 should set and clear the current voice's EON bit");

  auto bytes = makeKonamiSnesBuilderAram();
  bytes[0x4005] = 3;
  bytes[0x401c + 5] = 7;
  writeLe16(bytes, 0x2000, 0x2002);
  writeBytes(bytes, 0x2002, std::array<u8, 9>{0xe3, 0x2a, 0xe2, 0x04, 0xe3, 0x2c, 0xe2, 0x00, 0xff});
  const KonamiSnesLayout layout{
      .version = KONAMISNES_V6,
      .sequenceHeaderAddress = 0x2000,
      .spcDirAddress = 0x5000,
      .commonInstrumentTableAddress = 0x4000,
      .bankedInstrumentTableAddress = 0x4200,
      .firstBankedInstrument = 5,
      .percussionInstrumentTableAddress = 0x4300,
  };
  const auto instrumentPan = renderKonamiSnesAramSequence(bytes, layout);
  const auto pans = performanceEvents<StereoBalancePerformanceEvent>(instrumentPan.tracks.front());
  expect(pans.size() == 2 && std::abs(pans.back()->leftGain - 0x0e / 256.0) < 0.0001,
         "instrument loads should apply row pan only while the persistent instrument-pan flag is enabled");

  bytes = makeKonamiSnesBuilderAram();
  writeLe16(bytes, 0x2000, 0x2002);
  writeBytes(bytes, 0x2002, std::array<u8, 8>{0xfc, 0x80, 0x04, 0x3c, 1, 0x7f, 0x7f, 0xff});
  const auto combined = renderKonamiSnesAramSequence(bytes, layout);
  const auto levels = performanceEvents<LevelPerformanceEvent>(combined.tracks.front());
  expect(!levels.empty() && std::abs(levels.back()->linearGain - 2.0 / 127.0) < 0.0001,
         "FC should mix its new track volume with the newly loaded subtractive instrument volume");
}

void konamiSnesEveryVersionRendersSourceFreeCommands() {
  constexpr std::array<KonamiSnesVersion, 6> versions{
      KONAMISNES_V1, KONAMISNES_V2, KONAMISNES_V3, KONAMISNES_V4, KONAMISNES_V5, KONAMISNES_V6,
  };
  for (const KonamiSnesVersion version : versions) {
    const PerformanceSequence performance =
        renderKonamiSnesProgram(version, {{0xea, 0x80, 0x3c, 0x03, 0x7f, 0x7f, 0xe0, 0x02, 0xff}});
    expect(performance.diagnostics.empty() && performance.tracks.size() == 1 && performance.tracks[0].endTick == 5,
           "every Konami engine version should render the common tempo/note/rest command path");
  }
}

void konamiSnesSequenceSimulationPreservesDriverVibratoDepth() {
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V1, {{0xea, 0x80,              // tempo
                                               0xe4, 0x00, 0x40, 0x07,  // Axelay-style vibrato
                                               0xe0, 0x04, 0xff}});
  expect(performance.diagnostics.empty(), "KonamiSnes vibrato fixture should render without diagnostics");

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  s16 maximumBend = 0;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* bend = std::get_if<PitchBend>(&event)) {
      maximumBend = std::max<s16>(maximumBend, static_cast<s16>(std::abs(bend->value)));
    }
  }

  // Axelay's driver turns depth 7 into a peak offset of 7/32 semitones.
  // With MIDI's two-semitone bend range, that offset is a bend value of 896.
  expect(maximumBend == 896, "KonamiSnes sequence simulation should preserve the driver's full vibrato depth");
}

void konamiSnesCompiledPlaybackHandlesCallsLoopsTiesAndSlides() {
  const PerformanceSequence called = renderKonamiSnesProgram(KONAMISNES_V6, {{0xfe, 0x06, 0x00,  // call note subroutine
                                                                              0xe0, 0x02,        // rest after return
                                                                              0xff, 0x3c, 0x03, 0x7f, 0x7f, 0xff}});
  expect(called.diagnostics.empty() && called.tracks[0].endTick == 5,
         "compiled Konami call and context-sensitive end/return should preserve timing");
  expect(std::ranges::count_if(
             called.tracks[0].events,
             [](const PerformanceEvent& event) { return std::holds_alternative<NotePerformanceEvent>(event); }) == 1,
         "compiled Konami call should execute its decoded subroutine exactly once");

  const PerformanceSequence looped =
      renderKonamiSnesProgram(KONAMISNES_V6,
                              {{0xe6,                    // loop starts at the following note
                                0x3c, 0x04, 0x7f, 0x40,  // full-length note
                                0xe7, 0x02, 0x01, 0x01,  // play twice and change volume/pitch on replay
                                0xff}});
  const auto noteCount = std::ranges::count_if(looped.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(looped.diagnostics.empty() && looped.tracks[0].endTick == 8 && noteCount == 2,
         "both Konami loop counter state and accumulated replay changes should execute through the shared VM");

  const PerformanceSequence looped2 =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0xe8, 0x3c, 0x04, 0x7f, 0x40, 0xe9, 0x02, 0x00, 0x00, 0xff}});
  expect(looped2.diagnostics.empty() && looped2.tracks[0].endTick == 8 &&
             std::ranges::count_if(looped2.tracks[0].events,
                                   [](const PerformanceEvent& event) {
                                     return std::holds_alternative<NotePerformanceEvent>(event);
                                   }) == 2,
         "the second Konami loop counter should remain independent and replay through the shared VM");

  const PerformanceSequence volta =
      renderKonamiSnesProgram(KONAMISNES_V1, {{0xf6,                    // shared section starts here
                                               0xfe, 0x10, 0x00,        // shared section calls the note pattern
                                               0xf7,                    // first ending starts
                                               0x3d, 0x01, 0x64, 0x7f,  // first-ending note
                                               0xf7,                    // replay shared section
                                               0x3e, 0x01, 0x64, 0x7f,  // second-ending note
                                               0xf7, 0xff,              // replay shared section, then exit
                                               0x3c, 0x01, 0x64, 0x7f,  // shared note pattern
                                               0xff}});
  std::vector<double> voltaKeys;
  for (const PerformanceEvent& event : volta.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      voltaKeys.push_back(note->key);
    }
  }
  const std::vector<double> expectedVoltaKeys{60.0, 61.0, 60.0, 62.0, 60.0};
  expect(volta.diagnostics.empty() && volta.tracks[0].endTick == 5 && voltaKeys == expectedVoltaKeys,
         "a finite Konami volta branch should replay called patterns without being mistaken for a song loop");

  const PerformanceSequence tied =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0x3c, 0x04, 0x7f, 0x7f,  // full-length note enables slur
                                               0xbc, 0xff,              // compressed same note extends it
                                               0xe1, 0x02, 0x7f,        // explicit tie extends it again
                                               0xe0, 0x03,              // rest breaks the chain
                                               0xec, 0x02, 0xf2, 0x10,  // transpose and fine tuning
                                               0x3e, 0x02, 0x40, 0x7f,  // note with an inline late-engine slide
                                               0xf3, 0x00, 0x02, 0x40, 0, 0, 0xff}});
  expect(tied.diagnostics.empty() && tied.tracks[0].endTick == 15,
         "compressed notes, ties, rests, and inline pitch slides should preserve their combined wait time");
  const auto transition = std::ranges::find_if(tied.tracks[0].automations, [](const PerformanceAutomation& automation) {
    return pitchTransitionIntent(automation) != nullptr;
  });
  expect(transition != tied.tracks[0].automations.end() &&
             std::holds_alternative<LinearAutomationCurve>(pitchTransitionIntent(*transition)->curve) &&
             std::ranges::none_of(tied.tracks[0].events,
                                  [](const PerformanceEvent& event) {
                                    return std::holds_alternative<PitchBendPerformanceEvent>(event);
                                  }),
         "inline pitch slide should remain a shared linear transition");

  const MidiSequence exactPitchMidi = renderMidiSequence(tied);
  expect(std::ranges::any_of(exactPitchMidi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* bend = std::get_if<PitchBend>(&event);
                               return bend != nullptr && bend->value != 0;
                             }),
         "KonamiSnes should preserve its exact sampled curve as pitch bend by default");

  const MidiSequence nativePitchMidi =
      renderMidiSequence(tied, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::any_of(nativePitchMidi.tracks[0].events,
                             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) &&
             std::ranges::none_of(nativePitchMidi.tracks[0].events,
                                  [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }),
         "the shared linear transition should support native portamento");
}

void konamiSnesHeldNoteUsesRealizedInlineSlidePitch() {
  // Vampire Hunter, track 5 at ARAM $3a37. F3 reaches key $0e during
  // the first note; both following $0e notes keep the same DSP voice alive.
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0x0d, 0x60, 0x7f, 0x78,        // held key $0d
                                               0xf3, 0x06, 0x12, 0x0e, 5, 0,  // slide to key $0e
                                               0x8e, 0xf8,                    // compressed held key $0e
                                               0xef, 0xb4, 0x02,              // volume fade
                                               0x0e, 0xb4, 0x7d, 0x7f,        // another held key $0e
                                               0xff}});
  const auto notes = performanceEvents<NotePerformanceEvent>(performance.tracks[0]);
  expect(notes.size() == 3 && notes[1]->extendsPrevious && notes[2]->extendsPrevious &&
             notes[0]->note == notes[1]->note && notes[1]->note == notes[2]->note,
         "a held note at an inline slide's realized target should extend the sounding voice");
  expect(std::ranges::count_if(
             performance.tracks[0].automations,
             [](const PerformanceAutomation& automation) { return pitchTransitionIntent(automation) != nullptr; }) == 1,
         "a completed inline slide should not be repeated at the next held-note boundary");

  const MidiSequence pitchBend = renderMidiSequence(performance);
  expect(std::ranges::none_of(pitchBend.tracks[0].events,
                              [](const MidiEvent& event) {
                                const auto* bend = std::get_if<PitchBend>(&event);
                                return bend != nullptr && bend->tick == 0x60;
                              }),
         "the retained inline-slide bend should not be doubled at tick 96");

  const MidiSequence portamento =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::count_if(
             portamento.tracks[0].events,
             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) == 1 &&
             std::ranges::none_of(portamento.tracks[0].events,
                                  [](const MidiEvent& event) {
                                    const auto* note = std::get_if<NoteDuration>(&event);
                                    return note != nullptr && note->tick == 0x60;
                                  }),
         "native portamento should not retrigger and immediately silence the slide target at tick 96");
}

void konamiSnesHeldNoteRestartsPitchEnvelopeWithoutRetrigger() {
  // Nesting in the Sands, track 2 at ARAM $35c9/$40e9. Duration rate $65
  // suppresses key-off/key-on while each repeated note restarts F1.
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V1, {{0x4a, 0x24, 0x64, 0x38,  // ordinary key $4a
                                               0xf1, 0x00, 0xc1, 0x20,  // persistent pitch envelope
                                               0x4d, 0x06, 0x65, 0x65,  // held key $4d
                                               0x4d, 0x06, 0x65, 0x65,  // repeat without another attack
                                               0xff}});
  const auto notes = performanceEvents<NotePerformanceEvent>(performance.tracks[0]);
  expect(notes.size() == 3 && notes[1]->note == notes[2]->note && notes[2]->extendsPrevious,
         "a repeated held key should retain its sounding voice while the pitch envelope restarts");
  expect(std::ranges::count_if(
             performance.tracks[0].automations,
             [](const PerformanceAutomation& automation) { return pitchTransitionIntent(automation) != nullptr; }) == 2,
         "every held source note should restart the persistent pitch envelope");

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(std::ranges::none_of(midi.tracks[0].events,
                              [](const MidiEvent& event) {
                                const auto* note = std::get_if<NoteDuration>(&event);
                                return note != nullptr && note->tick == 42;
                              }),
         "restarting a held pitch envelope should not retrigger its MIDI note");
}

void konamiSnesCompiledAutomationTicksFades() {
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0xea, 0x80,              // tempo
                                               0xee, 0xff,              // volume
                                               0xe3, 0x14,              // pan
                                               0xe4, 0x00, 0x20, 0x10,  // vibrato
                                               0x3c, 0x01, 0x7f, 0x7f,  // establish nonzero note-side mixer state
                                               0xeb, 0x70, 0xfc,        // tempo fade by negative fixed step
                                               0xef, 0xc0, 0xfc,        // volume fade
                                               0xf8, 0x10, 0xff,        // pan fade
                                               0x3c, 0x08, 0x7f, 0x7f,  // sounding note advances all fades
                                               0xff}});
  const auto& events = performance.tracks[0].events;
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 9,
         "compiled Konami fades should advance only through the waiting command");
  expect(performance.tracks[0].automations.size() >= 3 &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* tempo = std::get_if<TempoPerformanceEvent>(&event);
                                   return tempo != nullptr && tempo->header.tick > 0;
                                 }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* level = std::get_if<LevelPerformanceEvent>(&event);
                                   return level != nullptr && level->header.tick > 0;
                                 }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* pan = std::get_if<StereoBalancePerformanceEvent>(&event);
                                   return pan != nullptr && pan->header.tick > 0;
                                 }),
         "tempo, volume, and pan fades should retain structured intent and exact per-tick realizations");
}

void konamiSnesPlayOnceCoordinatesGlobalLoopCompletion() {
  const PerformanceSequence performance = renderKonamiSnesProgram(
      KONAMISNES_V6, {
                         {0xe6, 0xe0, 0x04, 0xe7, 0x00, 0x01, 0x01},  // declared loop ignores finite-loop deltas
                         {0xe0, 0x0a, 0xff},                          // non-looping track ends at tick ten
                     });
  expect(performance.diagnostics.empty() && performance.tracks.size() == 2 && performance.tracks[0].endTick == 10 &&
             performance.tracks[1].endTick == 10,
         "play-once rendering should coordinate a Konami global loop boundary across all tracks");
  expect(std::ranges::none_of(
             performance.tracks[0].events,
             [](const PerformanceEvent& event) { return std::holds_alternative<TuningPerformanceEvent>(event); }),
         "declared Konami loops should not apply finite-loop pitch or volume deltas");

  const PerformanceSequence repeated =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0xe6, 0x3c, 0x04, 0x40, 0x7f, 0xe7, 0x00, 0x00, 0x00}}, 1);
  expect(repeated.diagnostics.empty() && repeated.tracks[0].endTick == 8 &&
             std::ranges::count_if(repeated.tracks[0].events,
                                   [](const PerformanceEvent& event) {
                                     return std::holds_alternative<NotePerformanceEvent>(event);
                                   }) == 2,
         "requested Konami sequence loops should replay the declared loop through shared loop policy");
  const MidiSequence repeatedMidi = renderMidiSequence(repeated);
  expect(std::ranges::count_if(repeatedMidi.tracks[0].events,
                               [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); }) == 2,
         "requested Konami loop playback should remain visible in default MIDI output");
}

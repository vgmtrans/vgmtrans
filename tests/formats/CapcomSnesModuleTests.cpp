/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/CapcomSnes/CapcomSnesModule.h"

#include "core/MidiExporter.h"
#include "core/PerformanceLowerer.h"
#include "core/ProjectSession.h"
#include "formats/CapcomSnes/CapcomSnesProfile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::capcom_snes;

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

void writeBe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value >> 8);
  bytes[offset + 1] = static_cast<u8>(value & 0xff);
}

template <size_t Size>
void writeBytes(std::vector<u8>& bytes, size_t offset, const std::array<u8, Size>& values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<u8> makeCapcomSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 16> readBgmAddressPattern{0x6f, 0x3f, 0xef, 0x06, 0x8f, 0x0d, 0xa1, 0x8f,
                                                     0xaf, 0xa0, 0x3f, 0x82, 0x05, 0x8d, 0x00, 0xdd};
  writeBytes(bytes, 0x0500, readBgmAddressPattern);
  bytes[0x0500 + 5] = 0x20;
  bytes[0x0500 + 8] = 0x00;

  constexpr std::array<u8, 12> loadInstrTablePattern{0x8d, 0x06, 0xcf, 0xda, 0xa0, 0x60,
                                                     0x98, 0xac, 0xa0, 0x98, 0x47, 0xa1};
  writeBytes(bytes, 0x0600, loadInstrTablePattern);
  bytes[0x0600 + 7] = 0x00;
  bytes[0x0600 + 10] = 0x40;

  constexpr std::array<u8, 16> dspRegInitPattern{0x8d, 0x03, 0xf6, 0x63, 0x04, 0xc5, 0xf2, 0x00,
                                                 0xf6, 0x66, 0x04, 0xc5, 0xf3, 0x00, 0xfe, 0xf2};
  writeBytes(bytes, 0x0700, dspRegInitPattern);
  bytes[0x0700 + 1] = 1;
  writeLe16(bytes, 0x0700 + 3, 0x0800);
  writeLe16(bytes, 0x0700 + 9, 0x0810);
  bytes[0x0801] = 0x5d;
  bytes[0x0811] = 0x50;

  for (int track = 0; track < 8; ++track) {
    writeBe16(bytes, 0x2001 + track * 2, 0x3000);
  }

  bytes[0x3000] = 0x05;
  bytes[0x3001] = 0x12;
  bytes[0x3002] = 0x34;
  bytes[0x3003] = 0x07;
  bytes[0x3004] = 0x40;
  bytes[0x3005] = 0x18;
  bytes[0x3006] = 0x00;
  bytes[0x3007] = 0x1a;
  bytes[0x3008] = 0x00;
  bytes[0x3009] = 0x20;
  bytes[0x300a] = 0x41;
  bytes[0x300b] = 0x17;

  bytes[0x4000] = 0x00;
  bytes[0x4001] = 0x8f;
  bytes[0x4002] = 0xe0;
  bytes[0x4003] = 0x00;
  writeBe16(bytes, 0x4004, 0x0100);

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x6000] = 0x01;

  return bytes;
}

}  // namespace

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples() {
  ProjectSession session;
  registerCapcomSnesModule(session.formats());
  registerCapcomSnesProfile(session.profiles());
  session.addSource(SourceFile{.name = "Mega Man X.spc"}, makeCapcomSnesAram());

  const Project project = session.scan();
  expect(project.diagnostics.empty(), "CapcomSnes scan should not report diagnostics for complete fixture");
  expect(project.collections.size() == 1, "CapcomSnes scan should produce one collection");
  expect(project.assets.size() == 3, "CapcomSnes scan should produce sequence, instrument bank, and samples");

  const auto* sequence = std::get_if<SequenceAsset>(&project.assets[0]);
  expect(sequence != nullptr, "first CapcomSnes asset should be sequence");
  expect(sequence->metadata.format == "CapcomSnes", "sequence should retain format name");
  expect(sequence->metadata.range.offset == 0x2001, "sequence range should point at fixed BGM header body");
  expect(sequence->program.timebase.ppqn == 48, "sequence should use CapcomSnes PPQN");
  expect(sequence->program.behavior.linearAmplitudeScale, "sequence should carry linear amplitude behavior");
  expect(sequence->program.behavior.writeInitialMonoMode, "sequence should carry mono mode behavior");
  expect(sequence->program.tracks.size() == 8, "sequence should decode all nonzero track pointers");
  expect(std::holds_alternative<TempoCommand>(sequence->program.tracks[0].commands[0]),
         "track should decode tempo command");
  expect(std::get<TempoCommand>(sequence->program.tracks[0].commands[0]).rawValue == 0x1234,
         "tempo command should preserve raw big-endian value");
  expect(std::holds_alternative<VolumeCommand>(sequence->program.tracks[0].commands[1]),
         "track should decode volume command");
  expect(std::holds_alternative<PanCommand>(sequence->program.tracks[0].commands[2]),
         "track should decode pan command");
  expect(std::holds_alternative<LfoCommand>(sequence->program.tracks[0].commands[3]),
         "track should decode LFO command");
  expect(std::holds_alternative<NoteCommand>(sequence->program.tracks[0].commands[4]),
         "track should decode note command");
  expect(std::holds_alternative<EndCommand>(sequence->program.tracks[0].commands[5]),
         "track should decode end command");

  const PerformanceSequence performance = PerformanceLowerer().lower(
      sequence->program, CapcomSnesProfile(CapcomSnesEngineVersion::v3BgmFixedLocation), LoopPolicy::PlayOnce);
  expect(performance.diagnostics.empty(), "CapcomSnes lowering should not warn for linear fixture");
  expect(performance.tracks.size() == 8, "lowerer should preserve track count");
  expect(performance.tracks[0].events.size() == 9, "lowered track should include initial, command, and end events");
  expect(std::holds_alternative<MonoMode>(performance.tracks[0].events[0]),
         "lowerer should emit initial mono mode from sequence behavior");
  expect(std::holds_alternative<Reverb>(performance.tracks[0].events[1]),
         "lowerer should emit initial reverb from sequence behavior");
  expect(std::get<Tempo>(performance.tracks[0].events[2]).microsecondsPerQuarter == 26369,
         "CapcomSnes profile should lower tempo with legacy timing math");
  expect(std::holds_alternative<Volume14>(performance.tracks[0].events[3]),
         "CapcomSnes V3 profile should lower volume to 14-bit volume");
  expect(std::get<Pan>(performance.tracks[0].events[4]).value == 64,
         "CapcomSnes center pan should lower to MIDI center pan");
  expect(std::holds_alternative<Expression>(performance.tracks[0].events[5]),
         "CapcomSnes pan lowering should include expression compensation");
  expect(std::get<VibratoDepth>(performance.tracks[0].events[6]).value == 0x20,
         "CapcomSnes LFO type 0 should lower to vibrato depth");
  expect(std::get<NoteDuration>(performance.tracks[0].events[7]).duration == 6,
         "CapcomSnes note length index should lower to ticks");
  expect(std::get<EndOfTrack>(performance.tracks[0].events[8]).tick == 6,
         "lowerer should advance time before end of track");

  const auto artifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                 .kinds = {ExportKind::Midi},
                                                                                 .loopPolicy = LoopPolicy::PlayOnce,
                                                                             });
  expect(artifacts.size() == 1, "value export should produce one MIDI artifact");
  expect(artifacts[0].filename == "Mega Man X.mid", "MIDI artifact should use collection name");
  expect(artifacts[0].mediaType == "audio/midi", "MIDI artifact should use audio/midi media type");
  expect(artifacts[0].diagnostics.empty(), "MIDI artifact should not carry diagnostics for linear fixture");
  expect(artifacts[0].bytes == MidiExporter().exportMidi(performance),
         "ProjectSession MIDI export should match direct lowerer/exporter output");

  const auto wavArtifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                    .kinds = {ExportKind::Wav},
                                                                                });
  expect(wavArtifacts.size() == 1, "value export should produce one WAV artifact for one sample");
  expect(wavArtifacts[0].filename == "Mega Man X-0-Sample 0.wav", "WAV artifact should include sample index and name");
  expect(wavArtifacts[0].mediaType == "audio/wav", "WAV artifact should use audio/wav media type");
  expect(wavArtifacts[0].diagnostics.empty(), "WAV artifact should not carry diagnostics for decodable sample");
  expect(wavArtifacts[0].bytes.size() == 76, "one BRR block should export as 44-byte header plus 32 PCM bytes");
  expect(std::vector<u8>(wavArtifacts[0].bytes.begin(), wavArtifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "WAV artifact should start with a RIFF header");
  expect(wavArtifacts[0].bytes[24] == 0x00 && wavArtifacts[0].bytes[25] == 0x7d,
         "WAV artifact should preserve the CapcomSnes sample rate");

  const auto sf2Artifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                    .kinds = {ExportKind::SoundFont2},
                                                                                });
  expect(sf2Artifacts.size() == 1, "value export should produce one SoundFont artifact");
  expect(sf2Artifacts[0].filename == "Mega Man X.sf2", "SoundFont artifact should use collection name");
  expect(sf2Artifacts[0].mediaType == "audio/soundfont", "SoundFont artifact should use audio/soundfont media type");
  expect(sf2Artifacts[0].diagnostics.empty(), "SoundFont artifact should not carry diagnostics for complete fixture");
  expect(sf2Artifacts[0].bytes.size() > 44, "SoundFont artifact should contain RIFF bytes");
  expect(std::vector<u8>(sf2Artifacts[0].bytes.begin(), sf2Artifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "SoundFont artifact should start with a RIFF header");
  expect(std::vector<u8>(sf2Artifacts[0].bytes.begin() + 8, sf2Artifacts[0].bytes.begin() + 12) ==
             std::vector<u8>{'s', 'f', 'b', 'k'},
         "SoundFont artifact should use sfbk RIFF type");

  const auto* instruments = std::get_if<InstrumentBankAsset>(&project.assets[1]);
  expect(instruments != nullptr, "second CapcomSnes asset should be instrument bank");
  expect(instruments->bank.instruments.size() == 1, "instrument bank should parse one valid instrument");
  expect(instruments->bank.instruments[0].program == 0, "instrument program should match table index");
  expect(instruments->bank.instruments[0].regions.size() == 1, "instrument should expose one region");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets[2]);
  expect(samples != nullptr, "third CapcomSnes asset should be sample collection");
  expect(samples->samples.samples.size() == 1, "sample collection should include referenced sample");
  expect(samples->samples.samples[0].codec == AudioCodec::SnesBrr, "sample should preserve BRR codec");
  expect(samples->samples.samples[0].encodedData.offset == 0x6000, "sample should point at encoded BRR bytes");
  expect(samples->samples.samples[0].encodedData.size == 9, "sample should preserve encoded BRR byte length");

  expect(project.collections[0].sequence == sequence->metadata.id, "collection should reference sequence");
  expect(project.collections[0].instrumentBanks == std::vector<AssetId>{instruments->metadata.id},
         "collection should reference instrument bank");
  expect(project.collections[0].sampleCollections == std::vector<AssetId>{samples->metadata.id},
         "collection should reference sample collection");
}

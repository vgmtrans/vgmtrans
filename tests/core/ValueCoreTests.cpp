/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/FormatModule.h"
#include "core/DlsExporter.h"
#include "core/MidiExporter.h"
#include "core/PerformanceLowerer.h"
#include "core/ProjectSession.h"
#include "core/SampleDecoder.h"
#include "core/SoundFontExporter.h"
#include "core/WavExporter.h"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace vgmtrans::core;

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
void capcomSnesPortamentoUsesSourceKeyDistanceUnderTranspose();

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

u32 readLe32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u32>(bytes[offset]) | (static_cast<u32>(bytes[offset + 1]) << 8) |
         (static_cast<u32>(bytes[offset + 2]) << 16) | (static_cast<u32>(bytes[offset + 3]) << 24);
}

bool containsAscii(const std::vector<u8>& bytes, std::string_view text) {
  return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

size_t asciiOffset(const std::vector<u8>& bytes, std::string_view text) {
  const auto found = std::search(bytes.begin(), bytes.end(), text.begin(), text.end());
  if (found == bytes.end()) {
    throw std::runtime_error("expected ASCII marker was not found");
  }
  return static_cast<size_t>(std::distance(bytes.begin(), found));
}

u32 chunkSize(const std::vector<u8>& bytes, std::string_view chunkId) {
  return readLe32(bytes, asciiOffset(bytes, chunkId) + 4);
}

class ProbeSequenceModule final : public FormatModule {
public:
  [[nodiscard]] std::string_view name() const override { return "ProbeSequence"; }

  [[nodiscard]] bool canScan(const SourceFile&, std::span<const u8> bytes) const override {
    return !bytes.empty() && bytes[0] == 0xaa;
  }

  [[nodiscard]] ScanResult scan(const ScanInput& input) const override {
    const auto assetId = input.ids.nextAssetId();
    const auto collectionId = input.ids.nextCollectionId();
    const auto itemId = input.ids.nextItemId();
    const auto assetRange = input.reader.range(0, input.reader.size());

    SequenceAsset sequence{
        .metadata =
            AssetMetadata{
                .id = assetId,
                .format = std::string(name()),
                .name = input.source.name,
                .range = assetRange,
                .items =
                    ItemTree{
                        .root = itemId,
                        .nodes = {ItemNode{
                            .id = itemId,
                            .kind = ItemKind::Sequence,
                            .detailKind = "probe-sequence",
                            .name = input.source.name,
                            .range = assetRange,
                        }},
                    },
            },
        .program =
            SequenceProgram{
                .tracks = {TrackProgram{
                    .id = TrackId{0},
                    .sourceTrackNumber = 0,
                    .startAddress = Address{0},
                    .commands = {EndCommand{.range = input.reader.range(0, 1)}},
                }},
            },
    };

    ScanResult result;
    result.assets.emplace_back(std::move(sequence));
    result.collections.push_back(Collection{
        .id = collectionId,
        .name = input.source.name,
        .sequence = assetId,
    });
    result.diagnostics.push_back(Diagnostic{
        .severity = Severity::Info,
        .message = "probe sequence scanned",
        .range = assetRange,
    });

    if (!input.source.virtualized) {
      result.extractedSources.push_back(ExtractedSource{
          .file = SourceFile{.name = input.source.name + ".child"},
          .bytes = {0xbb, 0x01},
          .origin = input.reader.range(0, 1),
      });
    }

    return result;
  }
};

class ProbeMiscModule final : public FormatModule {
public:
  [[nodiscard]] std::string_view name() const override { return "ProbeMisc"; }

  [[nodiscard]] bool canScan(const SourceFile& source, std::span<const u8> bytes) const override {
    return source.virtualized && !bytes.empty() && bytes[0] == 0xbb;
  }

  [[nodiscard]] ScanResult scan(const ScanInput& input) const override {
    return ScanResult{
        .assets = {MiscAsset{
            .metadata =
                AssetMetadata{
                    .format = std::string(name()),
                    .name = input.source.name,
                    .range = input.reader.range(0, input.reader.size()),
                },
            .payload = {input.reader.u8At(0), input.reader.u8At(1)},
        }},
    };
  }
};

void byteReaderChecksBoundsAndEndian() {
  const std::vector<u8> bytes{0x00, 0x34, 0x12, 0x78, 0x56};
  const ByteReader reader(SourceId{7}, bytes);

  expect(reader.has(1, 4), "reader should report valid four-byte range");
  expect(!reader.has(4, 2), "reader should reject range past end");
  expect(reader.u8At(1) == 0x34, "reader should read u8");
  expect(reader.le16(1) == 0x1234, "reader should read little-endian u16");
  expect(reader.be16(1) == 0x3412, "reader should read big-endian u16");
  expect(reader.le32(1) == 0x56781234, "reader should read little-endian u32");
  expect(reader.be32(1) == 0x34127856, "reader should read big-endian u32");

  bool threw = false;
  try {
    static_cast<void>(reader.u8At(5));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  expect(threw, "reader should throw on out-of-range access");
}

void projectSessionScansValuesAndVirtualSources() {
  ProjectSession session;
  session.formats().add(std::make_unique<ProbeSequenceModule>());
  session.formats().add(std::make_unique<ProbeMiscModule>());

  const auto sourceId = session.addSource(SourceFile{.name = "probe.spc"}, {0xaa, 0x34, 0x12});
  expect(sourceId == SourceId{0}, "first source should get SourceId 0");

  Project project = session.scan();
  expect(project.sources.size() == 2, "scan should include extracted virtual source");
  expect(project.sources[1].virtualized, "extracted source should be virtualized");
  expect(project.sources[1].origin.has_value() && project.sources[1].origin->source == sourceId &&
             project.sources[1].origin->offset == 0 && project.sources[1].origin->size == 1,
         "extracted virtual source should preserve its origin range");
  expect(project.assets.size() == 2, "scan should produce sequence and misc assets");
  expect(project.collections.size() == 1, "scan should produce one collection");
  expect(project.diagnostics.size() == 1, "scan should preserve module diagnostics");

  const auto* sequence = std::get_if<SequenceAsset>(&project.assets[0]);
  expect(sequence != nullptr, "first asset should be a sequence");
  expect(sequence->metadata.id == AssetId{0}, "sequence should keep allocated asset id");
  expect(sequence->metadata.items.nodes.size() == 1, "sequence should expose item tree");
  expect(project.collections[0].sequence == sequence->metadata.id, "collection should reference sequence asset");

  const auto* misc = std::get_if<MiscAsset>(&project.assets[1]);
  expect(misc != nullptr, "second asset should be misc from virtual source");
  expect(metadata(project.assets[1]).id == AssetId{1}, "missing asset id should be assigned");

  project = session.scan();
  expect(project.sources.size() == 2, "rescan should replace, not duplicate, virtual tail sources");
}

void snesBrrDecoderProducesPcm() {
  const std::vector<u8> sourceBytes{0x01, 0, 0, 0, 0, 0, 0, 0, 0};
  const Sample sample{
      .name = "zero",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 0, .size = sourceBytes.size()},
      .sampleRate = 32000,
  };

  const auto registry = SampleDecoderRegistry::withDefaultDecoders();
  const auto decoded = registry.decode(sample, sourceBytes);
  expect(decoded.has_value(), "BRR decoder should decode a valid sample");
  expect(decoded->sampleRate == 32000, "decoded sample should preserve sample rate");
  expect(decoded->pcm.size() == 16, "one BRR block should decode to 16 samples");
  expect(std::ranges::all_of(decoded->pcm, [](s16 sample) { return sample == 0; }),
         "zero BRR block should decode to silence");

  const Sample invalidRange = Sample{
      .name = "invalid",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 8, .size = 9},
  };
  expect(!registry.decode(invalidRange, sourceBytes).has_value(), "BRR decoder should reject invalid source ranges");
}

void midiExporterWritesStandardMidiFile() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .name = "Lead",
          .events =
              {
                  Tempo{.tick = 0, .microsecondsPerQuarter = 500000},
                  ProgramChange{.tick = 0, .channel = 0, .program = 5},
                  Volume{.tick = 0, .channel = 0, .value = 100},
                  NoteDuration{.tick = 0, .channel = 0, .key = 60, .velocity = 100, .duration = 24},
                  Pan{.tick = 12, .channel = 0, .value = 64},
                  EndOfTrack{.tick = 24},
              },
      }},
  };

  const std::vector<u8> expected{
      'M',  'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 'M',
      'T',  'r',  'k',  0x00, 0x00, 0x00, 0x26, 0x00, 0xff, 0x03, 0x04, 'L',  'e',  'a',  'd',
      0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20, 0x00, 0xc0, 0x05, 0x00, 0xb0, 0x07, 0x64, 0x00,
      0x90, 0x3c, 0x64, 0x0c, 0xb0, 0x0a, 0x40, 0x0c, 0x80, 0x3c, 0x00, 0x00, 0xff, 0x2f, 0x00,
  };

  const auto exported = MidiExporter().exportMidi(performance);
  expect(exported == expected, "MIDI exporter should write expected SMF bytes");
}

void performanceLowererSkipsCommandsAtPlayOnceLoopBoundary() {
  const auto range = [](u64 offset, u64 size) {
    return SourceRange{.source = SourceId{0}, .offset = offset, .size = size};
  };
  const SequenceProgram program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {TrackProgram{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0},
          .commands = {
              NoteCommand{.key = 60, .rawDuration = 12, .range = range(0, 1)},
              VolumeCommand{.rawValue = 99, .range = range(1, 1)},
              JumpCommand{.destination = Address{0}, .range = range(2, 3)},
              EndCommand{.range = range(5, 1)},
          },
      }},
  };

  const PerformanceSequence performance = PerformanceLowerer().lower(program, SequencerProfile{}, LoopPolicy::PlayOnce);
  const auto& events = performance.tracks[0].events;
  expect(std::ranges::any_of(events, [](const PerformanceEvent& event) {
           const auto* note = std::get_if<NoteDuration>(&event);
           return note != nullptr && note->tick == 0 && note->duration == 12;
         }),
         "play-once loop fixture should emit the note before the loop boundary");
  expect(std::ranges::none_of(events, [](const PerformanceEvent& event) {
           return std::holds_alternative<Volume>(event);
         }),
         "play-once lowering should skip commands exactly at the loop boundary");
}

void performanceLowererTreatsLoopBoundaryAsAStopPoint() {
  const auto range = [](u64 offset, u64 size) {
    return SourceRange{.source = SourceId{0}, .offset = offset, .size = size};
  };
  const SequenceProgram program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {TrackProgram{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0},
          .commands = {
              NoteCommand{.key = 60, .rawDuration = 12, .range = range(0, 1)},
              LoopBoundaryCommand{.destination = Address{1}, .trigger = Address{0}, .range = range(1, 0)},
              VolumeCommand{.rawValue = 99, .range = range(2, 1)},
          },
      }},
  };

  const PerformanceSequence performance = PerformanceLowerer().lower(program, SequencerProfile{}, LoopPolicy::PlayOnce);
  const auto& events = performance.tracks[0].events;
  expect(std::ranges::any_of(events, [](const PerformanceEvent& event) {
           const auto* note = std::get_if<NoteDuration>(&event);
           return note != nullptr && note->tick == 0 && note->duration == 12;
         }),
         "loop-boundary fixture should emit events before the boundary");
  expect(std::ranges::none_of(events, [](const PerformanceEvent& event) {
           return std::holds_alternative<Volume>(event);
         }),
         "loop-boundary fixture should not lower commands after the boundary");
}

void performanceLowererReplaysDecodedBoundaryUntilPlayOnceStop() {
  const auto range = [](u64 offset, u64 size) {
    return SourceRange{.source = SourceId{0}, .offset = offset, .size = size};
  };
  const SequenceProgram program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {
          TrackProgram{
              .id = TrackId{0},
              .sourceTrackNumber = 0,
              .startAddress = Address{0},
              .commands = {
                  NoteCommand{.key = 60, .rawDuration = 12, .range = range(0, 1)},
                  JumpCommand{.destination = Address{0}, .range = range(1, 3)},
              },
          },
          TrackProgram{
              .id = TrackId{1},
              .sourceTrackNumber = 1,
              .startAddress = Address{10},
              .commands = {
                  NoteCommand{.key = 64, .rawDuration = 12, .range = range(10, 1)},
                  JumpCommand{.destination = Address{20}, .range = range(11, 3)},
                  NoteCommand{.key = 65, .rawDuration = 12, .range = range(20, 1)},
                  LoopBoundaryCommand{.destination = Address{10}, .trigger = Address{20}, .range = range(21, 0)},
              },
          },
      },
  };

  const PerformanceSequence performance = PerformanceLowerer().lower(program, SequencerProfile{}, LoopPolicy::PlayOnce);
  const auto countNotesAt = [](const PerformanceTrack& track, u64 tick) {
    return std::ranges::count_if(track.events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NoteDuration>(&event);
      return note != nullptr && note->tick == tick;
    });
  };

  expect(countNotesAt(performance.tracks[0], 0) == 1 && countNotesAt(performance.tracks[0], 12) == 1 &&
             countNotesAt(performance.tracks[0], 24) == 1,
         "play-once lowering should replay earlier looped tracks until the shared stop tick");
  expect(countNotesAt(performance.tracks[1], 0) == 1 && countNotesAt(performance.tracks[1], 12) == 1 &&
             countNotesAt(performance.tracks[1], 24) == 1,
         "decoded loop boundaries should continue to their destination before the shared stop tick");
  expect(std::get<EndOfTrack>(performance.tracks[0].events.back()).tick == 36 &&
             std::get<EndOfTrack>(performance.tracks[1].events.back()).tick == 36,
         "replayed loop-boundary fixture should end both tracks at the shared stop tick");
}

void wavExporterWritesPcm16RiffFile() {
  const DecodedSample sample{
      .sampleRate = 8000,
      .channels = 1,
      .pcm = {-32768, 0, 32767},
  };

  const std::vector<u8> expected{
      'R',  'I',  'F',  'F',  0x2a, 0x00, 0x00, 0x00, 'W',  'A',  'V',  'E',  'f',  'm',  't',  ' ',  0x10,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x40, 0x1f, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x02, 0x00,
      0x10, 0x00, 'd',  'a',  't',  'a',  0x06, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0xff, 0x7f,
  };

  expect(WavExporter().exportPcm16(sample) == expected, "WAV exporter should write expected PCM16 RIFF bytes");
}

void soundFontExporterWritesSfbkRiffFile() {
  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  SampleCollectionAsset sampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 16},
              }},
          },
  };
  InstrumentBankAsset instrumentBank{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .bank =
          InstrumentBank{
              .instruments = {Instrument{
                  .bank = 1,
                  .program = 5,
                  .name = "Lead",
                  .regions = {Region{
                      .keyRange = KeyRange{.low = 24, .high = 96},
                      .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
                      .tuning = Tuning{.cents = 125},
                      .pan = 1.0,
                  }},
              }},
          },
  };

  const std::array<const InstrumentBankAsset*, 1> banks{&instrumentBank};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const auto result = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .instrumentBanks = banks,
          .sampleCollections = samples,
      },
      sources);

  expect(result.diagnostics.empty(), "SoundFont export should not report diagnostics for valid values");
  expect(result.bytes.size() > 44, "SoundFont export should produce RIFF bytes");
  expect(std::vector<u8>(result.bytes.begin(), result.bytes.begin() + 4) == std::vector<u8>{'R', 'I', 'F', 'F'},
         "SoundFont export should start with RIFF");
  expect(readLe32(result.bytes, 4) == result.bytes.size() - 8, "SoundFont RIFF size should match file size");
  expect(std::vector<u8>(result.bytes.begin() + 8, result.bytes.begin() + 12) == std::vector<u8>{'s', 'f', 'b', 'k'},
         "SoundFont RIFF type should be sfbk");
  expect(containsAscii(result.bytes, "INFO"), "SoundFont export should include INFO list");
  expect(containsAscii(result.bytes, "sdta"), "SoundFont export should include sample data list");
  expect(containsAscii(result.bytes, "pdta"), "SoundFont export should include preset data list");
  expect(containsAscii(result.bytes, "smpl"), "SoundFont export should include smpl chunk");
  expect(containsAscii(result.bytes, "phdr"), "SoundFont export should include phdr chunk");
  expect(containsAscii(result.bytes, "inst"), "SoundFont export should include inst chunk");
  expect(containsAscii(result.bytes, "shdr"), "SoundFont export should include shdr chunk");
  expect(containsAscii(result.bytes, "Lead"), "SoundFont export should include instrument name");
  expect(containsAscii(result.bytes, "Zero"), "SoundFont export should include sample name");
  expect(chunkSize(result.bytes, "smpl") == 124, "SoundFont smpl chunk should include PCM and SF2 padding samples");
  expect(chunkSize(result.bytes, "shdr") == 92, "SoundFont shdr chunk should include one sample and terminal record");
}

void dlsExporterWritesDlsRiffFile() {
  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  SampleCollectionAsset sampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 16},
              }},
          },
  };
  InstrumentBankAsset instrumentBank{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .bank =
          InstrumentBank{
              .instruments = {Instrument{
                  .bank = 1,
                  .program = 5,
                  .name = "Lead",
                  .regions = {Region{
                      .keyRange = KeyRange{.low = 24, .high = 96},
                      .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
                      .tuning = Tuning{.cents = 125},
                      .pan = 1.0,
                  }},
              }},
          },
  };

  const std::array<const InstrumentBankAsset*, 1> banks{&instrumentBank};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const auto result = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .instrumentBanks = banks,
          .sampleCollections = samples,
      },
      sources);

  expect(result.diagnostics.empty(), "DLS export should not report diagnostics for valid values");
  expect(result.bytes.size() > 44, "DLS export should produce RIFF bytes");
  expect(std::vector<u8>(result.bytes.begin(), result.bytes.begin() + 4) == std::vector<u8>{'R', 'I', 'F', 'F'},
         "DLS export should start with RIFF");
  expect(readLe32(result.bytes, 4) == result.bytes.size() - 8, "DLS RIFF size should match file size");
  expect(std::vector<u8>(result.bytes.begin() + 8, result.bytes.begin() + 12) == std::vector<u8>{'D', 'L', 'S', ' '},
         "DLS RIFF type should be DLS");
  expect(containsAscii(result.bytes, "colh"), "DLS export should include collection header");
  expect(containsAscii(result.bytes, "lins"), "DLS export should include instrument list");
  expect(containsAscii(result.bytes, "ptbl"), "DLS export should include pool table");
  expect(containsAscii(result.bytes, "wvpl"), "DLS export should include wave pool");
  expect(containsAscii(result.bytes, "wave"), "DLS export should include wave list");
  expect(containsAscii(result.bytes, "rgnh"), "DLS export should include region header");
  expect(containsAscii(result.bytes, "wsmp"), "DLS export should include sample metadata");
  expect(containsAscii(result.bytes, "wlnk"), "DLS export should include wave link");
  expect(containsAscii(result.bytes, "Lead"), "DLS export should include instrument name");
  expect(containsAscii(result.bytes, "Zero"), "DLS export should include sample name");
  expect(chunkSize(result.bytes, "colh") == 4, "DLS colh chunk should store one u32 count");
  expect(chunkSize(result.bytes, "ptbl") == 12, "DLS ptbl chunk should include one pool cue");
  expect(chunkSize(result.bytes, "data") == 32, "DLS data chunk should include decoded PCM bytes");
}

}  // namespace

int main() {
  try {
    byteReaderChecksBoundsAndEndian();
    projectSessionScansValuesAndVirtualSources();
    snesBrrDecoderProducesPcm();
    midiExporterWritesStandardMidiFile();
    performanceLowererSkipsCommandsAtPlayOnceLoopBoundary();
    performanceLowererTreatsLoopBoundaryAsAStopPoint();
    performanceLowererReplaysDecodedBoundaryUntilPlayOnceStop();
    wavExporterWritesPcm16RiffFile();
    soundFontExporterWritesSfbkRiffFile();
    dlsExporterWritesDlsRiffFile();
    capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
    capcomSnesPortamentoUsesSourceKeyDistanceUnderTranspose();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/DlsExporter.h"
#include "value/export/Export.h"
#include "value/core/FormatModule.h"
#include "value/export/MidiExporter.h"
#include "value/core/EventSequenceBuilder.h"
#include "value/core/Session.h"
#include "value/core/SampleDecoder.h"
#include "value/export/SoundFontExporter.h"
#include "value/export/WavExporter.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace vgmtrans::core;

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
void capcomSnesModuleScansSpcThroughVirtualAramSource();
void capcomSnesNoteStateCommandsAreTypedAndInterpreted();
void capcomSnesPortamentoUsesSourceKeyDistanceUnderTranspose();
void capcomSnesPanEventsDoNotRecurveMidiPan();
void capcomSnesV1VolumeQuantizesAfterAmplitudeCurve();
void capcomSnesMidiExportUsesSequenceProfileKey();

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

u16 readLe16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u16>(bytes[offset]) | (static_cast<u16>(bytes[offset + 1]) << 8);
}

s16 readLeS16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s16>(readLe16(bytes, offset));
}

s32 readLeS32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s32>(readLe32(bytes, offset));
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

bool soundFontIgenContainsAmount(const std::vector<u8>& bytes, u16 generator, s16 expectedAmount) {
  const auto chunkOffset = asciiOffset(bytes, "igen");
  const auto size = chunkSize(bytes, "igen");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 4 <= payloadOffset + size; offset += 4) {
    if (readLe16(bytes, offset) == generator && readLeS16(bytes, offset + 2) == expectedAmount) {
      return true;
    }
  }
  return false;
}

bool soundFontPgenContainsAmount(const std::vector<u8>& bytes, u16 generator, s16 expectedAmount) {
  const auto chunkOffset = asciiOffset(bytes, "pgen");
  const auto size = chunkSize(bytes, "pgen");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 4 <= payloadOffset + size; offset += 4) {
    if (readLe16(bytes, offset) == generator && readLeS16(bytes, offset + 2) == expectedAmount) {
      return true;
    }
  }
  return false;
}

bool soundFontBagAt(const std::vector<u8>& bytes, std::string_view chunkId, size_t index, u16 genIndex, u16 modIndex) {
  const auto chunkOffset = asciiOffset(bytes, chunkId);
  const auto size = chunkSize(bytes, chunkId);
  const auto offset = chunkOffset + 8 + (index * 4);
  if (offset + 4 > chunkOffset + 8 + size) {
    return false;
  }

  return readLe16(bytes, offset) == genIndex && readLe16(bytes, offset + 2) == modIndex;
}

bool soundFontImodContains(const std::vector<u8>& bytes, u16 source, u16 destination, s16 amount) {
  const auto chunkOffset = asciiOffset(bytes, "imod");
  const auto size = chunkSize(bytes, "imod");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 10 <= payloadOffset + size; offset += 10) {
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == destination &&
        readLeS16(bytes, offset + 4) == amount) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 destination, s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 source, u16 destination, s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 4) == destination &&
        readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(
    const std::vector<u8>& bytes,
    u16 source,
    u16 control,
    u16 destination,
    s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == control &&
        readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool sameRange(SourceRange lhs, SourceRange rhs) {
  return lhs.source == rhs.source && lhs.offset == rhs.offset && lhs.size == rhs.size;
}

const Diagnostic& diagnosticWithMessage(const std::vector<Diagnostic>& diagnostics, std::string_view message) {
  const auto found = std::ranges::find_if(diagnostics, [message](const Diagnostic& diagnostic) {
    return diagnostic.message == message;
  });
  if (found == diagnostics.end()) {
    throw std::runtime_error("expected diagnostic was not found");
  }
  return *found;
}

void expectDiagnosticRange(
    const std::vector<Diagnostic>& diagnostics,
    std::string_view message,
    SourceRange expectedRange) {
  const auto& diagnostic = diagnosticWithMessage(diagnostics, message);
  expect(diagnostic.range.has_value(), "diagnostic should preserve a source range");
  expect(sameRange(*diagnostic.range, expectedRange), "diagnostic should preserve the expected source range");
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
    const auto childItemId = input.ids.nextItemId();
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
                            .children = {ItemId{9999}},
                        },
                        ItemNode{
                            .id = childItemId,
                            .parent = itemId,
                            .kind = ItemKind::Header,
                            .detailKind = "probe-header",
                            .name = "Header",
                            .range = input.reader.range(0, 1),
                        }},
                    },
            },
        .program =
            CommandSequence{
                .tracks = {CommandTrack{
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

void sequencerCommandExposesSourceRange() {
  const SourceRange noteRange{.source = SourceId{3}, .offset = 0x1200, .size = 1};
  const SourceRange noteStateRange{.source = SourceId{3}, .offset = 0x1201, .size = 2};
  const SourceRange driverRange{.source = SourceId{3}, .offset = 0x1204, .size = 3};

  const Command note = NoteCommand{
      .key = 64,
      .rawVelocity = 90,
      .rawDuration = 4,
      .range = noteRange,
  };
  const Command noteState = NoteStateCommand{
      .action = NoteStateAction::Attributes,
      .rawValue = 0x48,
      .range = noteStateRange,
  };
  const Command driver = DriverSpecificCommand{
      .name = "Probe",
      .bytes = {0x01, 0x02, 0x03},
      .range = driverRange,
  };

  expect(sameRange(commandRange(note), noteRange), "command range should come from typed note command");
  expect(sameRange(commandRange(noteState), noteStateRange),
         "command range should come from typed note-state command");
  expect(sameRange(commandRange(driver), driverRange),
         "command range should come from typed driver-specific command");
  expect(defaultCommandName(note) == "Note", "default command name should describe typed note commands");
  expect(defaultCommandName(noteState) == "Note Attributes",
         "default command name should describe typed note-state actions");
  expect(defaultCommandName(driver) == "Probe", "default command name should preserve driver-specific names");
  expect(defaultCommandDetailKind(noteState) == "note-attributes",
         "default command detail kind should describe typed note-state actions");
  expect(defaultCommandDescription(note) == "Key 64, length index 4",
         "default command description should describe typed note commands");
}

void projectSessionScansValuesAndVirtualSources() {
  Session session;
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
  expect(assetById(project, sequence->metadata.id) == &project.assets[0],
         "project snapshot should find an asset by stable id");
  expect(assetById<SequenceAsset>(project, sequence->metadata.id) == sequence,
         "project snapshot should find a sequence asset by stable id");
  expect(assetById<MiscAsset>(project, sequence->metadata.id) == nullptr,
         "project snapshot should reject asset id lookups with the wrong value type");
  expect(assetById(project, AssetId{99}) == nullptr,
         "project snapshot should return null for a missing asset id");
  expect(assetById<SequenceAsset>(project, AssetId{99}) == nullptr,
         "project snapshot should return null for a missing asset id");
  expect(sequence->metadata.items.nodes.size() == 2, "sequence should expose item tree");
  expect(sequence->metadata.items.root == sequence->metadata.items.nodes[0].id,
         "scanner should preserve valid item tree root");
  expect(sequence->metadata.items.nodes[0].children == std::vector<ItemId>{sequence->metadata.items.nodes[1].id},
         "scanner should rebuild item children from parent links");
  expect(itemById(sequence->metadata.items, sequence->metadata.items.nodes[0].id) == &sequence->metadata.items.nodes[0],
         "item tree should find its root item by stable id");
  expect(itemById(sequence->metadata.items, sequence->metadata.items.nodes[1].id) == &sequence->metadata.items.nodes[1],
         "item tree should find child items by stable id");
  expect(itemById(sequence->metadata.items, ItemId{99}) == nullptr,
         "item tree should return null for a missing item id");
  expect(project.collections[0].sequence == sequence->metadata.id, "collection should reference sequence asset");
  expect(collectionById(project, project.collections[0].id) == &project.collections[0],
         "project snapshot should find a collection by stable id");
  expect(collectionById(project, CollectionId{99}) == nullptr,
         "project snapshot should return null for a missing collection id");

  const auto* misc = std::get_if<MiscAsset>(&project.assets[1]);
  expect(misc != nullptr, "second asset should be misc from virtual source");
  expect(metadata(project.assets[1]).id == AssetId{1}, "missing asset id should be assigned");

  project = session.scan();
  expect(project.sources.size() == 2, "rescan should replace, not duplicate, virtual tail sources");
}

void projectSessionAddsSourceFromPath() {
  const auto path = std::filesystem::temp_directory_path() / "vgmtrans-value-core-source-load.bin";
  std::filesystem::remove(path);
  {
    std::ofstream out(path, std::ios::binary);
    out.put(static_cast<char>(0xaa));
    out.put(static_cast<char>(0x34));
    out.put(static_cast<char>(0x12));
  }

  Session session;
  session.formats().add(std::make_unique<ProbeSequenceModule>());

  const auto sourceId = session.addSourceFromPath(path);
  expect(sourceId == SourceId{0}, "path source should get SourceId 0");
  expect(session.sources().source(sourceId).name == path.filename().string(),
         "path source should use the filename as source name");
  expect(session.sources().source(sourceId).path == path, "path source should preserve filesystem path");
  const std::array<u8, 3> expectedBytes{0xaa, 0x34, 0x12};
  expect(std::ranges::equal(session.sources().bytes(sourceId), expectedBytes),
         "path source should preserve file bytes");

  const Project project = session.scan();
  expect(project.collections.size() == 1, "path source should scan through registered modules");
  expect(project.sources.front().path == path, "project snapshot should preserve path source metadata");

  std::filesystem::remove(path);
}

void projectSessionExportsAllCollections() {
  Session session;
  session.formats().add(std::make_unique<ProbeSequenceModule>());

  session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  const Project project = session.scan();
  expect(project.collections.size() == 2, "probe sources should produce two collections");

  const auto exports = session.exportAllCollections(ExportRequest{
      .kinds = {ExportKind::Midi},
  });
  expect(exports.size() == project.collections.size(), "all-collection export should cover every collection");

  for (size_t i = 0; i < exports.size(); ++i) {
    expect(exports[i].collection == project.collections[i].id,
           "all-collection export should preserve collection ids in project order");
    expect(exports[i].artifacts.size() == 1, "probe MIDI export should return one artifact per collection");
    expect(exports[i].artifacts[0].filename == project.collections[i].name + ".mid",
           "collection export should keep collection-derived artifact names");
    expect(exports[i].artifacts[0].mediaType == "audio/midi", "collection export should keep artifact media types");
    expect(!exports[i].artifacts[0].diagnostics.empty(),
           "collection export diagnostics should stay attached to the artifact");
  }
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
  const EventSequence eventSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {EventTrack{
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

  const auto exported = MidiExporter().exportMidi(eventSequence);
  expect(exported == expected, "MIDI exporter should write expected SMF bytes");
}

void eventSequenceBuilderSkipsCommandsAtPlayOnceLoopBoundary() {
  const auto range = [](u64 offset, u64 size) {
    return SourceRange{.source = SourceId{0}, .offset = offset, .size = size};
  };
  const CommandSequence program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
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

  const EventSequence eventSequence = EventSequenceBuilder().build(program, SequencerProfile{}, LoopPolicy::PlayOnce);
  const auto& events = eventSequence.tracks[0].events;
  expect(std::ranges::any_of(events, [](const Event& event) {
           const auto* note = std::get_if<NoteDuration>(&event);
           return note != nullptr && note->tick == 0 && note->duration == 12;
         }),
         "play-once loop fixture should emit the note before the loop boundary");
  expect(std::ranges::none_of(events, [](const Event& event) {
           return std::holds_alternative<Volume>(event);
         }),
         "play-once event build should skip commands exactly at the loop boundary");
}

void eventSequenceBuilderResolvesUnsetDefaultLoopPolicyToPlayOnce() {
  const auto range = [](u64 offset, u64 size) {
    return SourceRange{.source = SourceId{0}, .offset = offset, .size = size};
  };
  const CommandSequence program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0},
          .commands = {
              NoteCommand{.key = 60, .rawDuration = 12, .range = range(0, 1)},
              JumpCommand{.destination = Address{0}, .range = range(1, 3)},
          },
      }},
  };

  const EventSequence eventSequence = EventSequenceBuilder().build(program, SequencerProfile{}, LoopPolicy::Default);
  const auto& events = eventSequence.tracks[0].events;
  expect(eventSequence.diagnostics.empty(), "unset default loop policy should not run until command cap");
  expect(std::ranges::count_if(events, [](const Event& event) {
           return std::holds_alternative<NoteDuration>(event);
         }) == 1,
         "unset default loop policy should build self-looping tracks once");
  expect(std::get<EndOfTrack>(events.back()).tick == 12,
         "unset default loop policy should end at the first playthrough boundary");
}

void eventSequenceBuilderTreatsLoopBoundaryAsAStopPoint() {
  const auto range = [](u64 offset, u64 size) {
    return SourceRange{.source = SourceId{0}, .offset = offset, .size = size};
  };
  const CommandSequence program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
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

  const EventSequence eventSequence = EventSequenceBuilder().build(program, SequencerProfile{}, LoopPolicy::PlayOnce);
  const auto& events = eventSequence.tracks[0].events;
  expect(std::ranges::any_of(events, [](const Event& event) {
           const auto* note = std::get_if<NoteDuration>(&event);
           return note != nullptr && note->tick == 0 && note->duration == 12;
         }),
         "loop-boundary fixture should emit events before the boundary");
  expect(std::ranges::none_of(events, [](const Event& event) {
           return std::holds_alternative<Volume>(event);
         }),
         "loop-boundary fixture should not build commands after the boundary");
}

void eventSequenceBuilderReplaysDecodedBoundaryUntilPlayOnceStop() {
  const auto range = [](u64 offset, u64 size) {
    return SourceRange{.source = SourceId{0}, .offset = offset, .size = size};
  };
  const CommandSequence program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {
          CommandTrack{
              .id = TrackId{0},
              .sourceTrackNumber = 0,
              .startAddress = Address{0},
              .commands = {
                  NoteCommand{.key = 60, .rawDuration = 12, .range = range(0, 1)},
                  JumpCommand{.destination = Address{0}, .range = range(1, 3)},
              },
          },
          CommandTrack{
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

  const EventSequence eventSequence = EventSequenceBuilder().build(program, SequencerProfile{}, LoopPolicy::PlayOnce);
  const auto countNotesAt = [](const EventTrack& track, u64 tick) {
    return std::ranges::count_if(track.events, [tick](const Event& event) {
      const auto* note = std::get_if<NoteDuration>(&event);
      return note != nullptr && note->tick == tick;
    });
  };

  expect(countNotesAt(eventSequence.tracks[0], 0) == 1 && countNotesAt(eventSequence.tracks[0], 12) == 1 &&
             countNotesAt(eventSequence.tracks[0], 24) == 1,
         "play-once event build should replay earlier looped tracks until the shared stop tick");
  expect(countNotesAt(eventSequence.tracks[1], 0) == 1 && countNotesAt(eventSequence.tracks[1], 12) == 1 &&
             countNotesAt(eventSequence.tracks[1], 24) == 1,
         "decoded loop boundaries should continue to their destination before the shared stop tick");
  expect(std::get<EndOfTrack>(eventSequence.tracks[0].events.back()).tick == 36 &&
             std::get<EndOfTrack>(eventSequence.tracks[1].events.back()).tick == 36,
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
  InstrumentSetAsset instrumentSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .instruments = {Instrument{
          .bank = 1,
          .program = 5,
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .tuning = Tuning{.cents = 125},
              .envelope = Envelope{
                  .attack = 1'000'000,
                  .decay = 2'000'000,
                  .sustain = 500,
                  .release = 250'000,
              },
              .pan = 1.0,
          }},
          .generators = {
              SynthGenerator{.destination = SynthDestination::VibratoDepth, .amount = 120},
              SynthGenerator{.destination = SynthDestination::VibratoRate, .amount = 240},
          },
          .modulators = {
              SynthModulator{
                  .source = SynthSource::NoteOnVelocity,
                  .destination = SynthDestination::VibratoDepth,
                  .amount = 300,
              },
              SynthModulator{
                  .source = SynthSource::ChannelPressure,
                  .destination = SynthDestination::VibratoRate,
                  .amount = 0,
              },
              SynthModulator{
                  .destination = SynthDestination::TremoloRate,
                  .amount = 180,
              },
          },
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&instrumentSet};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const auto result = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
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
  expect(chunkSize(result.bytes, "pgen") == 12, "SoundFont pgen chunk should include reverb, instrument, and terminal generators");
  expect(soundFontBagAt(result.bytes, "pbag", 1, 2, 0), "SoundFont terminal preset bag should include both preset generators");
  expect(soundFontPgenContainsAmount(result.bytes, 16, 250),
         "SoundFont export should write default preset reverb send");
  expect(chunkSize(result.bytes, "ibag") == 12, "SoundFont ibag chunk should include a global generator zone");
  expect(soundFontBagAt(result.bytes, "ibag", 0, 0, 0), "SoundFont global zone should start at generator index 0");
  expect(soundFontBagAt(result.bytes, "ibag", 1, 2, 3),
         "SoundFont region zone should start after instrument generators and modulators");
  expect(soundFontBagAt(result.bytes, "ibag", 2, 16, 3),
         "SoundFont terminal bag should include all generators and modulators");
  expect(chunkSize(result.bytes, "imod") == 40, "SoundFont imod chunk should include modulators plus terminal");
  expect(soundFontImodContains(result.bytes, 2, 6, 300),
         "SoundFont export should write explicit velocity-to-vibrato modulator");
  expect(soundFontImodContains(result.bytes, 13, 24, 0),
         "SoundFont export should write explicit channel-pressure-to-vibrato-rate modulator");
  expect(soundFontImodContains(result.bytes, 203, 22, 180),
         "SoundFont export should resolve default tremolo-rate source from the destination");
  expect(chunkSize(result.bytes, "igen") == 68, "SoundFont igen chunk should include global and region generators");
  expect(chunkSize(result.bytes, "shdr") == 92, "SoundFont shdr chunk should include one sample and terminal record");
  expect(soundFontIgenContainsAmount(result.bytes, 6, 120),
         "SoundFont export should write instrument vibrato depth generator");
  expect(soundFontIgenContainsAmount(result.bytes, 24, 240),
         "SoundFont export should write instrument vibrato rate generator");
  expect(soundFontIgenContainsAmount(result.bytes, 34, 0),
         "SoundFont export should write attackVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 35, -32768),
         "SoundFont export should write holdVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 36, 1200),
         "SoundFont export should write decayVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 37, 60),
         "SoundFont export should write sustainVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 38, -2400),
         "SoundFont export should write releaseVolEnv from Region envelope");
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
  InstrumentSetAsset instrumentSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .instruments = {Instrument{
          .bank = 1,
          .program = 5,
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .tuning = Tuning{.cents = 125},
              .envelope = Envelope{
                  .attack = 1'000'000,
                  .decay = 2'000'000,
                  .sustain = 500,
                  .release = 250'000,
              },
              .pan = 1.0,
          }},
          .generators = {
              SynthGenerator{.destination = SynthDestination::VibratoDepth, .amount = 120},
              SynthGenerator{.destination = SynthDestination::VibratoRate, .amount = 240},
          },
          .modulators = {
              SynthModulator{
                  .source = SynthSource::NoteOnVelocity,
                  .destination = SynthDestination::VibratoDepth,
                  .amount = 300,
              },
              SynthModulator{
                  .source = SynthSource::ChannelPressure,
                  .destination = SynthDestination::VibratoRate,
                  .amount = 0,
              },
              SynthModulator{
                  .destination = SynthDestination::TremoloRate,
                  .amount = 180,
              },
          },
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&instrumentSet};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const auto result = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
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
  expect(containsAscii(result.bytes, "art2"), "DLS export should include region articulation");
  expect(containsAscii(result.bytes, "Lead"), "DLS export should include instrument name");
  expect(containsAscii(result.bytes, "Zero"), "DLS export should include sample name");
  expect(chunkSize(result.bytes, "colh") == 4, "DLS colh chunk should store one u32 count");
  expect(chunkSize(result.bytes, "ptbl") == 12, "DLS ptbl chunk should include one pool cue");
  expect(chunkSize(result.bytes, "data") == 32, "DLS data chunk should include decoded PCM bytes");
  expect(chunkSize(result.bytes, "art2") == 140,
         "DLS art2 chunk should include pan, envelope, generator, and modulator connections");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0206, 0),
         "DLS export should write EG1 attack time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x020c, std::numeric_limits<s32>::min()),
         "DLS export should write EG1 hold time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0207, 78643200),
         "DLS export should write EG1 decay time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x020a, 61425937),
         "DLS export should write EG1 sustain level from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0209, -157286400),
         "DLS export should write EG1 release time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0009, 0x0003, 7864320),
         "DLS export should write instrument vibrato depth generator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0000, 0x0114, 15728640),
         "DLS export should write instrument vibrato rate generator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0009, 0x0002, 0x0003, 19660800),
         "DLS export should write explicit velocity-to-vibrato modulator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0008, 0x0114, 0),
         "DLS export should write explicit channel-pressure-to-vibrato-rate modulator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0008, 0x0104, 11796480),
         "DLS export should resolve default tremolo-rate source from the destination");
}

void exportDiagnosticsPreserveSourceRanges() {
  SourceStore sources;
  const auto validSource = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  const SourceRange missingSampleRange{.source = SourceId{99}, .offset = 0x12, .size = 9};
  SampleCollectionAsset missingSampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Missing Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Missing",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = missingSampleRange,
              }},
          },
  };

  Project project;
  project.assets.push_back(missingSampleCollection);
  project.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Probe",
      .sampleCollections = {missingSampleCollection.metadata.id},
  });

  SequencerProfileRegistry profiles;
  const auto wavArtifacts = ExportService().exportCollection(
      project, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Wav}}, profiles);
  expect(wavArtifacts.size() == 1, "WAV export should return one artifact for one sample");
  expectDiagnosticRange(wavArtifacts[0].diagnostics, "Sample source was not found", missingSampleRange);

  const std::array<const SampleCollectionAsset*, 1> missingSamples{&missingSampleCollection};
  const auto sf2MissingSample = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .sampleCollections = missingSamples,
      },
      sources);
  expectDiagnosticRange(sf2MissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const auto dlsMissingSample = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .sampleCollections = missingSamples,
      },
      sources);
  expectDiagnosticRange(dlsMissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const SourceRange sampleRange{.source = validSource, .offset = 0, .size = 9};
  const SourceRange regionRange{.source = validSource, .offset = 0x40, .size = 6};
  SampleCollectionAsset validSampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{3},
              .format = "Probe",
              .name = "Valid Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = sampleRange,
                  .sampleRate = 16000,
              }},
          },
  };
  InstrumentSetAsset badRegionSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Bad Region Set",
          },
      .instruments = {Instrument{
          .bank = 0,
          .program = 0,
          .name = "Lead",
          .regions = {Region{
              .sample = SampleRef{.collection = validSampleCollection.metadata.id, .index = 9},
              .range = regionRange,
          }},
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&badRegionSet};
  const std::array<const SampleCollectionAsset*, 1> validSamples{&validSampleCollection};
  const auto sf2BadRegion = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(sf2BadRegion.diagnostics, "Region sample reference was not found", regionRange);

  const auto dlsBadRegion = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(dlsBadRegion.diagnostics, "Region sample reference was not found", regionRange);
}

}  // namespace

int main() {
  try {
    byteReaderChecksBoundsAndEndian();
    sequencerCommandExposesSourceRange();
    projectSessionScansValuesAndVirtualSources();
    projectSessionAddsSourceFromPath();
    projectSessionExportsAllCollections();
    snesBrrDecoderProducesPcm();
    midiExporterWritesStandardMidiFile();
    eventSequenceBuilderSkipsCommandsAtPlayOnceLoopBoundary();
    eventSequenceBuilderResolvesUnsetDefaultLoopPolicyToPlayOnce();
    eventSequenceBuilderTreatsLoopBoundaryAsAStopPoint();
    eventSequenceBuilderReplaysDecodedBoundaryUntilPlayOnceStop();
    wavExporterWritesPcm16RiffFile();
    soundFontExporterWritesSfbkRiffFile();
    dlsExporterWritesDlsRiffFile();
    exportDiagnosticsPreserveSourceRanges();
    capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
    capcomSnesModuleScansSpcThroughVirtualAramSource();
    capcomSnesNoteStateCommandsAreTypedAndInterpreted();
    capcomSnesPortamentoUsesSourceKeyDistanceUnderTranspose();
    capcomSnesPanEventsDoNotRecurveMidiPan();
    capcomSnesV1VolumeQuantizesAfterAmplitudeCurve();
    capcomSnesMidiExportUsesSequenceProfileKey();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

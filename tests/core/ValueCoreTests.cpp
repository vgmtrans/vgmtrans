/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/FormatModule.h"
#include "core/MidiExporter.h"
#include "core/ProjectSession.h"
#include "core/SampleDecoder.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vgmtrans::core;

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
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
        .metadata = AssetMetadata{
            .id = assetId,
            .format = std::string(name()),
            .name = input.source.name,
            .range = assetRange,
            .items = ItemTree{
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
        .program = SequenceProgram{
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
            .metadata = AssetMetadata{
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
  expect(project.assets.size() == 2, "scan should produce sequence and misc assets");
  expect(project.collections.size() == 1, "scan should produce one collection");
  expect(project.diagnostics.size() == 1, "scan should preserve module diagnostics");

  const auto* sequence = std::get_if<SequenceAsset>(&project.assets[0]);
  expect(sequence != nullptr, "first asset should be a sequence");
  expect(sequence->metadata.id == AssetId{0}, "sequence should keep allocated asset id");
  expect(sequence->metadata.items.nodes.size() == 1, "sequence should expose item tree");
  expect(project.collections[0].sequence == sequence->metadata.id,
         "collection should reference sequence asset");

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
  expect(!registry.decode(invalidRange, sourceBytes).has_value(),
         "BRR decoder should reject invalid source ranges");
}

void midiExporterWritesStandardMidiFile() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .name = "Lead",
          .events = {
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
      'M',  'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30,
      'M',  'T',  'r',  'k',  0x00, 0x00, 0x00, 0x26,
      0x00, 0xff, 0x03, 0x04, 'L',  'e',  'a',  'd',
      0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20,
      0x00, 0xc0, 0x05,
      0x00, 0xb0, 0x07, 0x64,
      0x00, 0x90, 0x3c, 0x64,
      0x0c, 0xb0, 0x0a, 0x40,
      0x0c, 0x80, 0x3c, 0x00,
      0x00, 0xff, 0x2f, 0x00,
  };

  const auto exported = MidiExporter().exportMidi(performance);
  expect(exported == expected, "MIDI exporter should write expected SMF bytes");
}

}  // namespace

int main() {
  try {
    byteReaderChecksBoundsAndEndian();
    projectSessionScansValuesAndVirtualSources();
    snesBrrDecoderProducesPcm();
    midiExporterWritesStandardMidiFile();
    capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

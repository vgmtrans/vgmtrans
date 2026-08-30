/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TriAcePS1/TriAcePS1.h"

#include "value/base/RecordReader.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace vgmtrans::formats::triace_ps1 {

using namespace core;

namespace {

constexpr u32 kBankHeaderSize = 12;
constexpr u32 kInstrumentHeaderSize = 8;
constexpr u32 kRegionSize = 20;
// The driver's pitch table is 0x0fcd at index 59 and reaches unity between
// indices 59 and 60. This is the exact source-domain root before region tuning.
constexpr double kDriverUnityKey = 59.216912152;

struct ParsedRegion {
  KeyRange keys;
  VelocityRange velocities;
  u32 sampleOffset = 0;
  u32 loopOffset = 0;
  u8 level = 0;
  s8 semitone = 0;
  s8 fine = 0;
  SourceRecord source;
};

struct ParsedInstrument {
  TriAcePs1Instrument identity;
  SourceRecord source;
  std::vector<ParsedRegion> regions;
};

[[nodiscard]] std::vector<ParsedInstrument> readInstruments(ByteReader reader, const TriAcePs1BankLayout& layout,
                                                            std::vector<Diagnostic>* diagnostics) {
  std::vector<ParsedInstrument> instruments;
  const u32 end = layout.offset + layout.instrumentSectionSize - 4;
  for (u32 cursor = layout.offset + kBankHeaderSize; cursor < end;) {
    const u8 count = reader.u8At(cursor + 7);
    const u32 recordSize = kInstrumentHeaderSize + static_cast<u32>(count) * kRegionSize;
    RecordReader header(reader, cursor, cursor + kInstrumentHeaderSize, diagnostics);
    ParsedInstrument instrument{
        .identity =
            TriAcePs1Instrument{
                .program = *header.u8At(0, "program", SourceValueDisplay::Hex),
                .bank = *header.u8At(1, "bank", SourceValueDisplay::Hex),
                .adsr1 = *header.u16leAt(2, "adsr1", SourceValueDisplay::Hex),
                .adsr2 = *header.u16leAt(4, "adsr2", SourceValueDisplay::Hex),
            },
    };
    static_cast<void>(header.u8At(6, "unknown_06", SourceValueDisplay::Hex));
    static_cast<void>(header.u8At(7, "region_count"));
    instrument.source = std::move(header).finish();
    instrument.regions.reserve(count);

    for (u32 index = 0; index < count; ++index) {
      const u32 regionOffset = cursor + kInstrumentHeaderSize + index * kRegionSize;
      RecordReader region(reader, regionOffset, regionOffset + kRegionSize, diagnostics);
      ParsedRegion parsed{
          .keys =
              KeyRange{
                  .low = *region.u8At(0, "key_low", SourceValueDisplay::MidiNote),
                  .high = *region.u8At(1, "key_high", SourceValueDisplay::MidiNote),
              },
          .velocities =
              VelocityRange{
                  .low = *region.u8At(2, "velocity_low"),
                  .high = *region.u8At(3, "velocity_high"),
              },
          .sampleOffset = *region.u32leAt(4, "sample_offset", SourceValueDisplay::Address),
          .loopOffset = *region.u32leAt(8, "loop_offset", SourceValueDisplay::Address),
          .level = *region.u8At(12, "level"),
          .semitone = *region.s8At(13, "semitone_tune"),
          .fine = *region.s8At(14, "fine_tune"),
      };
      static_cast<void>(region.u8At(15, "unknown_0f", SourceValueDisplay::Hex));
      static_cast<void>(region.u32leAt(16, "unknown_10", SourceValueDisplay::Hex));
      parsed.source = std::move(region).finish();
      instrument.regions.push_back(std::move(parsed));
    }
    instruments.push_back(std::move(instrument));
    cursor += recordSize;
  }
  return instruments;
}

}  // namespace

std::optional<TriAcePs1ScannedBank> addTriAcePs1Bank(ScanResultBuilder& result, const TriAcePs1BankLayout& layout) {
  const ByteReader reader = result.reader();
  auto parsed = readInstruments(reader, layout, &result.diagnostics());
  std::set<u32> offsets;
  u8 maximumLevel = 0;
  for (const auto& instrument : parsed) {
    for (const auto& region : instrument.regions) {
      if (region.sampleOffset < layout.sampleSectionSize) {
        offsets.insert(region.sampleOffset);
      }
      maximumLevel = std::max(maximumLevel, region.level);
    }
  }
  if (offsets.empty() || maximumLevel == 0) {
    return std::nullopt;
  }

  const u32 sampleEnd = layout.sampleSectionOffset + layout.sampleSectionSize;
  std::map<u32, PsxAdpcmStream> streams;
  for (auto current = offsets.begin(); current != offsets.end(); ++current) {
    const u32 boundary =
        std::next(current) == offsets.end() ? sampleEnd : layout.sampleSectionOffset + *std::next(current);
    if (const auto stream = inspectPsxAdpcmStream(reader, layout.sampleSectionOffset + *current, boundary)) {
      streams.emplace(*current, *stream);
    }
  }
  if (streams.empty()) {
    return std::nullopt;
  }

  const std::string name = fmt::format("TriAcePS1 Bank {:X}", layout.offset);
  auto bank = result.soundBank(name, reader.range(layout.offset, layout.length));
  auto& instruments = bank.instruments();
  auto& samples = bank.localSamples();
  const SourceRange bankRange = reader.range(layout.offset, layout.length);
  instruments.include(bankRange);
  samples.include(bankRange);

  instruments
      .source(SourceRole::Header, "TriAcePS1 Bank Header", reader.range(layout.offset, kBankHeaderSize),
              "triace-ps1-bank-header")
      .field("size", reader.range(layout.offset, 4), layout.length)
      .field("instrument_section_size", reader.range(layout.offset + 4, 2), layout.instrumentSectionSize)
      .field("unknown_06", reader.range(layout.offset + 6, 2), layout.unknown06, SourceValueDisplay::Hex)
      .field("unknown_08", reader.range(layout.offset + 8, 2), layout.unknown08, SourceValueDisplay::Hex)
      .field("unknown_0a", reader.range(layout.offset + 10, 2), layout.unknown0a, SourceValueDisplay::Hex);
  const SourceAnnotationId instrumentRoot =
      instruments
          .source(SourceRole::Table, "Instrument Table",
                  reader.range(layout.offset + kBankHeaderSize, layout.instrumentSectionSize - kBankHeaderSize),
                  "triace-ps1-instrument-table")
          .id();
  const SourceAnnotationId sampleRoot =
      samples
          .source(SourceRole::SamplePool, "SPU Sample Data",
                  reader.range(layout.sampleSectionOffset, layout.sampleSectionSize), "triace-ps1-sample-data")
          .id();

  std::map<u32, SampleRef> sampleRefs;
  for (const auto& [offset, stream] : streams) {
    const u32 index = static_cast<u32>(sampleRefs.size());
    auto entry = samples.add(offset, Sample{
                                         .name = fmt::format("Sample {}", index),
                                         .codec = AudioCodec::PsxAdpcm,
                                         .encodedData = stream.encodedData,
                                         .sampleRate = kPs1SpuSampleRate,
                                         .channels = 1,
                                         .bitsPerSample = 16,
                                         .loop = stream.loop,
                                     });
    entry.source(fmt::format("Sample {}", index), stream.encodedData, "psx-adpcm-sample").parent(sampleRoot);
    sampleRefs.emplace(offset, entry.ref());
  }

  for (auto& source : parsed) {
    const u16 encoded = static_cast<u16>((source.identity.bank << 8) | source.identity.program);
    auto instrument = instruments.append(Instrument{
        .explicitAddress =
            InstrumentAddress{
                .bank = static_cast<u32>(encoded >> 7),
                .program = static_cast<u32>(encoded & 0x7f),
            },
        .identity = triAcePs1InstrumentIdentity(source.identity.bank, source.identity.program),
        .name = fmt::format("Instrument {:02X}:{:02X}", source.identity.bank, source.identity.program),
        .range = reader.range(source.source.range.offset,
                              kInstrumentHeaderSize + static_cast<u32>(source.regions.size()) * kRegionSize),
    });
    instrument.source(instrument.value().name, source.source, "triace-ps1-instrument").parent(instrumentRoot);

    for (const auto& sourceRegion : source.regions) {
      const auto sample = sampleRefs.find(sourceRegion.sampleOffset);
      if (sample == sampleRefs.end()) {
        continue;
      }
      Region region{
          .keyRange = sourceRegion.keys,
          .velocityRange = sourceRegion.velocities,
          .range = sourceRegion.source.range,
          .unityKey = kDriverUnityKey - sourceRegion.semitone - sourceRegion.fine / 64.0,
          .envelope = psxSpuEnvelope(source.identity.adsr1, source.identity.adsr2),
          // The driver multiplies this byte directly. Normalize the loudest
          // region in a bank so values above 0x7f retain their intended boost.
          .attenuationDb = linearAmplitudeToAttenuationDb(sourceRegion.level / static_cast<double>(maximumLevel)),
      };
      const auto stream = streams.find(sourceRegion.sampleOffset);
      if (stream != streams.end() && sourceRegion.loopOffset >= sourceRegion.sampleOffset) {
        const u32 encodedLoop = sourceRegion.loopOffset - sourceRegion.sampleOffset;
        if (encodedLoop < stream->second.encodedData.size) {
          const u32 frames = psxAdpcmDecodedFrames(static_cast<u32>(stream->second.encodedData.size));
          const u32 loopStart = psxAdpcmDecodedOffset(encodedLoop);
          region.loop = Loop{
              .enabled = stream->second.loop.enabled,
              .start = loopStart,
              .length = loopStart < frames ? frames - loopStart : 0,
          };
        }
      }
      instrument.region(sample->second, std::move(region)).source("Region", sourceRegion.source, "triace-ps1-region");
    }
  }

  return TriAcePs1ScannedBank{.bank = bank};
}

}  // namespace vgmtrans::formats::triace_ps1

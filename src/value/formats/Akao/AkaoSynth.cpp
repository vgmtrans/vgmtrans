/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"

#include "value/base/RecordReader.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

struct ArticulationTable {
  u32 articulationTableOffset = 0;
  u32 articulationSize = 0;
  u32 sampleSectionOffset = 0;
  u32 sampleSectionSize = 0;
  u32 firstArticulationId = 0;
  u32 articulationCount = 0;
  std::optional<u16> sampleSetId;
};

struct ParsedSample {
  u32 sourceOffset = 0;
  Sample value;
};

struct ParsedSamplePool {
  ScanSamplePoolRef ref;
  std::vector<AkaoArticulation> articulations;
  std::vector<ParsedSample> samples;
  std::string name;
  SourceRange range;
  ArticulationTable table;
};

[[nodiscard]] double log2Cents(double multiplier) {
  return multiplier > 0.0 ? std::log(multiplier) / std::log(2.0) * 1200.0 : 0.0;
}

[[nodiscard]] s8 coarseTuneFromCents(double cents) {
  return static_cast<s8>(cents / 100.0);
}

[[nodiscard]] s16 fineTuneFromCents(double cents) {
  return static_cast<s16>(static_cast<int>(cents) % 100);
}

[[nodiscard]] std::optional<ArticulationTable> sampleHeader(ByteReader reader, u32 offset, AkaoPs1Version version) {
  if (version == AkaoPs1Version::Unknown) {
    return std::nullopt;
  }
  const AkaoProfile profile{.version = version};
  ArticulationTable table;
  if (profile.version3OrLater()) {
    if (!reader.has(offset, 0x40)) {
      return std::nullopt;
    }
    table.sampleSetId = reader.le16(offset + 4);
    table.sampleSectionSize = reader.le32(offset + 0x14);
    table.firstArticulationId = reader.le32(offset + 0x18);
    table.articulationCount = reader.le32(offset + 0x1c);
    table.articulationSize = profile.articulationSize();
    table.articulationTableOffset = offset + 0x40;
  } else if (profile.hasLegacySampleHeader()) {
    if (!reader.has(offset, 0x40)) {
      return std::nullopt;
    }
    table.sampleSectionSize = reader.le32(offset + 0x14);
    table.firstArticulationId = reader.le32(offset + 0x18);
    const u32 endingArticulationId = profile.legacySampleEndingArticulationId(reader, offset);
    if (endingArticulationId < table.firstArticulationId) {
      return std::nullopt;
    }
    table.articulationCount = endingArticulationId - table.firstArticulationId;
    table.articulationSize = 0x40;
    table.articulationTableOffset = offset + 0x40;
  } else {
    return std::nullopt;
  }

  if (table.articulationCount == 0 || table.articulationCount > 300 ||
      !reader.has(table.articulationTableOffset, table.articulationSize * table.articulationCount)) {
    return std::nullopt;
  }
  table.sampleSectionOffset = table.articulationTableOffset + table.articulationSize * table.articulationCount;
  if (table.sampleSectionOffset > reader.size()) {
    return std::nullopt;
  }
  table.sampleSectionSize =
      static_cast<u32>(std::min<u64>(table.sampleSectionSize, reader.size() - table.sampleSectionOffset));
  while (table.sampleSectionSize >= kPsxAdpcmBlockBytes &&
         reader.has(table.sampleSectionOffset + table.sampleSectionSize - kPsxAdpcmBlockBytes, 4) &&
         reader.le32(table.sampleSectionOffset + table.sampleSectionSize - kPsxAdpcmBlockBytes) == 0) {
    table.sampleSectionSize -= kPsxAdpcmBlockBytes;
  }
  return table;
}

[[nodiscard]] ArticulationTable splitSampleHeader(ByteReader reader, AkaoSplitSampleLocation location) {
  ArticulationTable table{
      .articulationTableOffset = location.articulationTableOffset,
      .articulationSize = 0x40,
      .sampleSectionOffset = location.sampleHeaderOffset + 0x10,
      .sampleSectionSize =
          reader.has(location.sampleHeaderOffset + 4, 4) ? reader.le32(location.sampleHeaderOffset + 4) : 0,
      .firstArticulationId = location.firstArticulationId,
      .articulationCount = location.articulationCount,
  };
  if (table.sampleSectionOffset < reader.size()) {
    table.sampleSectionSize =
        static_cast<u32>(std::min<u64>(table.sampleSectionSize, reader.size() - table.sampleSectionOffset));
  } else {
    table.sampleSectionSize = 0;
  }
  while (table.sampleSectionSize >= kPsxAdpcmBlockBytes &&
         reader.has(table.sampleSectionOffset + table.sampleSectionSize - kPsxAdpcmBlockBytes, 4) &&
         reader.le32(table.sampleSectionOffset + table.sampleSectionSize - kPsxAdpcmBlockBytes) == 0) {
    table.sampleSectionSize -= kPsxAdpcmBlockBytes;
  }
  return table;
}

[[nodiscard]] std::optional<AkaoArticulation> readArticulation(ByteReader reader, const ArticulationTable& table,
                                                               AkaoPs1Version version, u32 index,
                                                               u32 spuDestinationAddress) {
  const AkaoProfile profile{.version = version};
  const u32 articulationOffset = table.articulationTableOffset + index * table.articulationSize;
  AkaoArticulation articulation{
      .articulationId = table.firstArticulationId + index,
  };
  RecordReader record(reader, articulationOffset, articulationOffset + table.articulationSize);
  const auto finishSource = [&]() { return std::move(record).finish(); };
  if (profile.hasCompactArticulations()) {
    // The later driver stores a signed fixed-point pitch multiplier. Convert
    // it once to the root-key and fine-tuning values expected by synth export.
    const u32 sampleOffset = *record.u32leAt(0, "sample_offset", SourceValueDisplay::Address);
    const u32 loopAddress = *record.u32leAt(4, "loop_address", SourceValueDisplay::Address);
    const s16 rawFineTune = *record.s16leAt(8, "pitch_multiplier");
    const u16 unityKey = *record.u16leAt(0x0a, "unity_key", SourceValueDisplay::MidiNote);
    const u16 adsr1 = *record.u16leAt(0x0c, "adsr1", SourceValueDisplay::Hex);
    const u16 adsr2 = *record.u16leAt(0x0e, "adsr2", SourceValueDisplay::Hex);
    const double multiplier =
        rawFineTune >= 0 ? 1.0 + (rawFineTune / 32768.0) : (static_cast<u16>(rawFineTune) / 65536.0);
    const double cents = log2Cents(multiplier);
    const s8 coarse = coarseTuneFromCents(cents);
    articulation.sampleOffset = sampleOffset;
    articulation.loopPoint = loopAddress - articulation.sampleOffset;
    articulation.fineTuneCents = fineTuneFromCents(cents);
    articulation.unityKey = static_cast<u8>(unityKey - coarse);
    articulation.adsr1 = adsr1;
    articulation.adsr2 = adsr2;
    articulation.source = finishSource();
    return articulation;
  }

  if (version == AkaoPs1Version::Version3_0) {
    // Version 3.0 uses the older expanded articulation record, but its sample
    // addresses are already relative to this collection's sample section.
    const u32 sampleOffset = *record.u32leAt(0, "sample_offset", SourceValueDisplay::Address);
    const u32 loopAddress = *record.u32leAt(4, "loop_address", SourceValueDisplay::Address);
    const u32 pitch = *record.u32leAt(8, "pitch_multiplier");
    const u8 attackRate = *record.u8At(0x38, "attack_rate");
    const u8 decayRate = *record.u8At(0x39, "decay_rate");
    const u8 sustainLevel = *record.u8At(0x3a, "sustain_level");
    const u8 sustainRate = *record.u8At(0x3b, "sustain_rate");
    const u8 releaseRate = *record.u8At(0x3c, "release_rate");
    const u8 attackMode = *record.u8At(0x3d, "attack_mode", SourceValueDisplay::Hex);
    const u8 sustainMode = *record.u8At(0x3e, "sustain_mode", SourceValueDisplay::Hex);
    const u8 releaseMode = *record.u8At(0x3f, "release_mode", SourceValueDisplay::Hex);
    const double cents = log2Cents(pitch / static_cast<double>(4096 * 256));
    const s8 coarse = coarseTuneFromCents(cents);
    articulation.sampleOffset = sampleOffset;
    articulation.loopPoint = loopAddress - articulation.sampleOffset;
    articulation.fineTuneCents = fineTuneFromCents(cents);
    articulation.unityKey = static_cast<u8>(72 - coarse);
    articulation.adsr1 = composePsxAdsr1((attackMode & 4) >> 2, attackRate, decayRate, sustainLevel);
    articulation.adsr2 = composePsxAdsr2((sustainMode & 4) >> 2, (sustainMode & 2) >> 1, sustainRate,
                                         (releaseMode & 4) >> 2, releaseRate);
    articulation.source = finishSource();
    return articulation;
  }

  // Earlier drivers store absolute SPU addresses. The sample pool header
  // tells us where the upload begins, so normalize both addresses back to
  // offsets within the encoded sample data.
  const u32 sampleStartAddress = *record.u32leAt(0, "sample_address", SourceValueDisplay::Address);
  const u32 loopStartAddress = *record.u32leAt(4, "loop_address", SourceValueDisplay::Address);
  const u8 attackRate = *record.u8At(8, "attack_rate");
  const u8 decayRate = *record.u8At(9, "decay_rate");
  const u8 sustainLevel = *record.u8At(0x0a, "sustain_level");
  const u8 sustainRate = *record.u8At(0x0b, "sustain_rate");
  const u8 releaseRate = *record.u8At(0x0c, "release_rate");
  const u8 attackMode = *record.u8At(0x0d, "attack_mode", SourceValueDisplay::Hex);
  const u8 sustainMode = *record.u8At(0x0e, "sustain_mode", SourceValueDisplay::Hex);
  const u8 releaseMode = *record.u8At(0x0f, "release_mode", SourceValueDisplay::Hex);
  const u32 pitch = *record.u32leAt(0x10, "pitch_multiplier");
  if (sampleStartAddress < spuDestinationAddress || loopStartAddress < spuDestinationAddress ||
      sampleStartAddress > loopStartAddress) {
    return std::nullopt;
  }
  const double cents = log2Cents(pitch / 4096.0);
  const s8 coarse = coarseTuneFromCents(cents);
  articulation.sampleOffset = sampleStartAddress - spuDestinationAddress;
  articulation.loopPoint = loopStartAddress - sampleStartAddress;
  articulation.fineTuneCents = fineTuneFromCents(cents);
  articulation.unityKey = static_cast<u8>(72 - coarse);
  articulation.adsr1 = composePsxAdsr1((attackMode & 4) >> 2, attackRate, decayRate, sustainLevel);
  articulation.adsr2 =
      composePsxAdsr2((sustainMode & 4) >> 2, (sustainMode & 2) >> 1, sustainRate, (releaseMode & 4) >> 2, releaseRate);
  articulation.source = finishSource();
  return articulation;
}

[[nodiscard]] std::optional<ParsedSamplePool> parseSamplePoolWithTable(const ScanInput& input, u32 offset, u32 length,
                                                                       AkaoPs1Version version, ArticulationTable table,
                                                                       std::string name) {
  if (table.articulationCount == 0 || table.sampleSectionSize == 0) {
    return std::nullopt;
  }
  const u32 spuDestinationAddress = AkaoProfile{.version = version}.spuDestinationAddress(input.reader, offset);
  std::vector<AkaoArticulation> articulations;
  articulations.reserve(table.articulationCount);
  for (u32 i = 0; i < table.articulationCount; ++i) {
    if (auto articulation = readArticulation(input.reader, table, version, i, spuDestinationAddress)) {
      articulations.push_back(*articulation);
    }
  }
  if (articulations.empty()) {
    return std::nullopt;
  }

  std::set<u32> sampleOffsets;
  for (const auto& articulation : articulations) {
    sampleOffsets.insert(articulation.sampleOffset);
  }

  std::map<u32, u32> sampleIndexByOffset;
  std::vector<ParsedSample> samples;
  const u32 sampleSectionEnd = table.sampleSectionOffset + table.sampleSectionSize;
  for (const u32 sampleOffset : sampleOffsets) {
    const u32 sampleAddress = table.sampleSectionOffset + sampleOffset;
    if (sampleAddress >= sampleSectionEnd || !input.reader.has(sampleAddress, 1)) {
      continue;
    }
    const auto sampleInfo = inspectPsxAdpcmStream(input.reader, sampleAddress, sampleSectionEnd);
    if (!sampleInfo) {
      continue;
    }
    const u32 sampleIndex = static_cast<u32>(samples.size());
    sampleIndexByOffset.emplace(sampleOffset, sampleIndex);
    samples.push_back(ParsedSample{
        .sourceOffset = sampleOffset,
        .value =
            Sample{
                .name = fmt::format("Sample {}", sampleIndex),
                .codec = AudioCodec::PsxAdpcm,
                .encodedData = sampleInfo->encodedData,
                .sampleRate = kPs1SpuSampleRate,
                .channels = 1,
                .bitsPerSample = 16,
                .loop = sampleInfo->loop,
            },
    });
  }
  if (samples.empty()) {
    return std::nullopt;
  }

  // Loop intent is split between ADPCM block flags and the articulation table.
  // Preserve block flags when complete, and use the articulation point only to
  // supply information the sample stream omitted.
  for (auto& articulation : articulations) {
    if (auto found = sampleIndexByOffset.find(articulation.sampleOffset); found != sampleIndexByOffset.end()) {
      articulation.sampleIndex = found->second;
      const u32 encodedLength = static_cast<u32>(samples[articulation.sampleIndex].value.encodedData.size);
      Loop& sampleLoop = samples[articulation.sampleIndex].value.loop;
      if (sampleLoop.enabled && sampleLoop.start == 0 && sampleLoop.length == 0) {
        const u32 loopStart =
            articulation.loopPoint < encodedLength ? psxAdpcmDecodedOffset(articulation.loopPoint) : 0;
        articulation.loop = Loop{
            .enabled = true,
            .start = loopStart,
            .length =
                loopStart < psxAdpcmDecodedFrames(encodedLength) ? psxAdpcmDecodedFrames(encodedLength) - loopStart : 0,
        };
      } else if (!sampleLoop.enabled && sampleLoop.start == 0 && sampleLoop.length == 0 &&
                 articulation.loopPoint != 0 && articulation.loopPoint < encodedLength) {
        const u32 loopStart = psxAdpcmDecodedOffset(articulation.loopPoint);
        sampleLoop.start = loopStart;
        sampleLoop.length =
            loopStart < psxAdpcmDecodedFrames(encodedLength) ? psxAdpcmDecodedFrames(encodedLength) - loopStart : 0;
      }
    }
  }

  const SourceRange range = input.reader.range(offset, length);
  return ParsedSamplePool{
      .articulations = std::move(articulations),
      .samples = std::move(samples),
      .name = std::move(name),
      .range = range,
      .table = table,
  };
}

void emitSamplePool(const ScanInput& input, ScanResultBuilder& result, ParsedSamplePool& parsed) {
  auto samples = result.samplePool(parsed.name, parsed.range);
  parsed.ref = samples.ref();
  const SourceAnnotationId root =
      samples.source(SourceRole::SamplePool, parsed.name, parsed.range, "akao-sample-collection").id();
  for (auto& parsedSample : parsed.samples) {
    // Source offsets may be sparse and shared by many articulations. The
    // builder keeps that lookup separate from the dense sample indexes stored
    // in the finished collection.
    const std::string name = parsedSample.value.name;
    const SourceRange range = parsedSample.value.encodedData;
    samples.add(parsedSample.sourceOffset, std::move(parsedSample.value))
        .source(name, range, "psx-adpcm-sample")
        .parent(root);
  }
  const SourceRange articulationTableRange = input.reader.range(
      parsed.table.articulationTableOffset, parsed.table.articulationSize * parsed.table.articulationCount);
  const SourceAnnotationId articulationTable = result.sourceMap()
                                                   .table("Akao Articulation Table", articulationTableRange)
                                                   .kind("akao-articulation-table")
                                                   .parent(root)
                                                   .derived("first_articulation_id", parsed.table.firstArticulationId)
                                                   .derived("articulation_count", parsed.table.articulationCount)
                                                   .id();
  for (const AkaoArticulation& articulation : parsed.articulations) {
    auto annotation = result.sourceMap()
                          .annotation(SourceRole::TableEntry,
                                      fmt::format("Articulation {}", articulation.articulationId), articulation.source)
                          .kind("akao-articulation")
                          .parent(articulationTable)
                          .derived("articulation_id", articulation.articulationId)
                          .derived("effective_unity_key", articulation.unityKey, SourceValueDisplay::MidiNote)
                          .derived("fine_tune_cents", articulation.fineTuneCents, SourceValueDisplay::Cents)
                          .derived("effective_sample_offset", articulation.sampleOffset, SourceValueDisplay::Address)
                          .derived("loop_point", articulation.loopPoint, SourceValueDisplay::Address)
                          .derived("effective_adsr1", articulation.adsr1, SourceValueDisplay::Hex)
                          .derived("effective_adsr2", articulation.adsr2, SourceValueDisplay::Hex);
    if (articulation.sampleIndex < parsed.samples.size()) {
      annotation.link(SourceLinkRole::UsesSample,
                      SourceTarget{ObjectRefs::sample(parsed.ref.id, articulation.sampleIndex)});
    }
  }
  samples.data(AkaoSamplePoolData{
      .sampleSetId = parsed.table.sampleSetId,
      .firstArticulationId = parsed.table.firstArticulationId,
      .articulationCount = parsed.table.articulationCount,
      .articulations = std::move(parsed.articulations),
  });
}

[[nodiscard]] std::optional<ParsedSamplePool> parseSamplePoolValues(const ScanInput& input, u32 offset,
                                                                    AkaoPs1Version version) {
  if (version == AkaoPs1Version::Unknown) {
    version = guessSampleVersion(input.reader, offset);
  }
  auto table = sampleHeader(input.reader, offset, version);
  if (!table) {
    return std::nullopt;
  }
  const u32 length = static_cast<u32>(
      std::min<u64>(input.reader.size() - offset, table->sampleSectionOffset + table->sampleSectionSize - offset));
  return parseSamplePoolWithTable(
      input, offset, length, version, *table,
      fmt::format("Akao Sample Collection {:02X}", table->sampleSetId.value_or(input.reader.le16(offset + 4))));
}

[[nodiscard]] std::optional<ParsedSamplePool> parseSamplePoolValues(const ScanInput& input,
                                                                    AkaoSplitSampleLocation location) {
  if (!input.reader.has(location.sampleHeaderOffset, 8) || !input.reader.has(location.articulationTableOffset, 1)) {
    return std::nullopt;
  }
  auto table = splitSampleHeader(input.reader, location);
  if (!input.reader.has(table.articulationTableOffset, table.articulationSize * table.articulationCount) ||
      table.sampleSectionSize == 0) {
    return std::nullopt;
  }
  const u32 offset = std::min(location.sampleHeaderOffset, location.articulationTableOffset);
  const u32 endOffset = std::max(table.sampleSectionOffset + table.sampleSectionSize,
                                 table.articulationTableOffset + table.articulationSize * table.articulationCount);
  return parseSamplePoolWithTable(input, offset, endOffset - offset, AkaoPs1Version::Version1_0, table,
                                  "Akao Sample Collection FF7");
}

}  // namespace

std::optional<AkaoSplitSampleLocation> ff7HardcodedAkaoSampleLocation(ByteReader reader) {
  if (reader.size() >= 0x1a8000 && reader.has(0xe0000, 4) && reader.has(0x156000, 4) &&
      reader.le32(0xe0000) == 0x1010 && reader.le32(0x156000) == 0x1010) {
    return AkaoSplitSampleLocation{
        .sampleHeaderOffset = 0xe0000,
        .articulationTableOffset = 0x156000,
        .firstArticulationId = 0,
        .articulationCount = 128,
    };
  }
  return std::nullopt;
}

bool isPossibleAkaoSamplePool(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 0x50) || reader.be32(offset) != kAkaoSignature || reader.le16(offset + 6) != 0) {
    return false;
  }
  if ((reader.le32(offset + 0x24) != 0 || reader.le32(offset + 0x28) != 0 || reader.le32(offset + 0x2c) != 0) &&
      (reader.le32(offset + 0x30) != 0 || reader.le32(offset + 0x34) != 0 || reader.le32(offset + 0x38) != 0) &&
      reader.le32(offset + 0x3c) != 0) {
    return false;
  }
  const u32 firstDest = reader.le32(offset + 0x40);
  return firstDest == 0 || firstDest == reader.le32(offset + 0x10);
}

bool parseAkaoSamplePool(const ScanInput& input, ScanResultBuilder& result, u32 offset, AkaoPs1Version version) {
  auto parsed = parseSamplePoolValues(input, offset, version);
  if (!parsed) {
    return false;
  }
  emitSamplePool(input, result, *parsed);
  return true;
}

bool parseAkaoSamplePool(const ScanInput& input, ScanResultBuilder& result, AkaoSplitSampleLocation location) {
  auto parsed = parseSamplePoolValues(input, location);
  if (!parsed) {
    return false;
  }
  emitSamplePool(input, result, *parsed);
  return true;
}

}  // namespace vgmtrans::formats::akao

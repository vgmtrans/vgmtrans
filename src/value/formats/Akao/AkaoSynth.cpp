/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoSynth.h"

#include "value/formats/Akao/AkaoVersion.h"

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

constexpr u32 kPsxSampleRate = 44100;
constexpr u32 kPsxAdpcmBlockBytes = 16;
constexpr u32 kPsxAdpcmBlockSamples = 28;

struct ArticulationTable {
  u32 artsOffset = 0;
  u32 artSize = 0;
  u32 sampleSectionOffset = 0;
  u32 sampleSectionSize = 0;
  u32 firstArtId = 0;
  u32 artCount = 0;
  std::optional<u16> sampleSetId;
};

struct PsxSampleInfo {
  u32 encodedLength = 0;
  Loop loop;
};

struct ParsedSampleCollection {
  AkaoSampleCollectionParse parse;
  SampleCollection samples;
  std::string name;
  SourceRange range;
  ArticulationTable table;
};

[[nodiscard]] s16 leS16(ByteReader reader, u32 offset) {
  return static_cast<s16>(reader.le16(offset));
}

[[nodiscard]] u16 composePsxAdsr1(u8 attackMode, u8 attackRate, u8 decayRate, u8 sustainLevel) {
  return static_cast<u16>(((attackMode & 1) << 15) | ((attackRate & 0x7f) << 8) | ((decayRate & 0x0f) << 4) |
                          (sustainLevel & 0x0f));
}

[[nodiscard]] u16 composePsxAdsr2(u8 sustainMode, u8 sustainDirection, u8 sustainRate, u8 releaseMode, u8 releaseRate) {
  return static_cast<u16>(((sustainMode & 1) << 15) | ((sustainDirection & 1) << 14) | ((sustainRate & 0x7f) << 6) |
                          ((releaseMode & 1) << 5) | (releaseRate & 0x1f));
}

[[nodiscard]] double log2Cents(double multiplier) {
  return multiplier > 0.0 ? std::log(multiplier) / std::log(2.0) * 1200.0 : 0.0;
}

[[nodiscard]] s8 coarseTuneFromCents(double cents) {
  return static_cast<s8>(cents / 100.0);
}

[[nodiscard]] s16 fineTuneFromCents(double cents) {
  return static_cast<s16>(static_cast<int>(cents) % 100);
}

[[nodiscard]] u32 psxDecodedFrames(u32 encodedBytes) {
  return (encodedBytes / kPsxAdpcmBlockBytes) * kPsxAdpcmBlockSamples;
}

[[nodiscard]] u32 psxDecodedOffset(u32 encodedOffset) {
  return (encodedOffset / kPsxAdpcmBlockBytes) * kPsxAdpcmBlockSamples;
}

[[nodiscard]] PsxSampleInfo psxSampleInfo(ByteReader reader, u32 offset, u32 endOffset) {
  u32 cursor = offset;
  std::optional<u32> loopStartBytes;
  bool loops = false;
  while (cursor + kPsxAdpcmBlockBytes <= endOffset && reader.has(cursor, kPsxAdpcmBlockBytes)) {
    const u8 flags = reader.u8At(cursor + 1);
    if ((flags & 4) != 0) {
      loopStartBytes = cursor - offset;
    }
    cursor += kPsxAdpcmBlockBytes;
    if ((flags & 1) != 0) {
      loops = (flags & 2) != 0;
      const u32 encodedLength = cursor - offset;
      return PsxSampleInfo{
          .encodedLength = encodedLength,
          .loop =
              Loop{
                  .enabled = loops,
                  .start = loopStartBytes ? psxDecodedOffset(*loopStartBytes) : 0,
                  .length = loopStartBytes ? psxDecodedOffset(encodedLength - *loopStartBytes) : 0,
              },
      };
    }
  }
  const u32 encodedLength = cursor > offset ? cursor - offset : 0;
  return PsxSampleInfo{
      .encodedLength = encodedLength,
      .loop =
          Loop{
              .enabled = false,
              .start = loopStartBytes ? psxDecodedOffset(*loopStartBytes) : 0,
              .length = loopStartBytes ? psxDecodedOffset(encodedLength - *loopStartBytes) : 0,
          },
  };
}

[[nodiscard]] std::optional<ArticulationTable> sampleHeader(ByteReader reader, u32 offset, AkaoPs1Version version) {
  const AkaoProfile profile = akaoProfile(version);
  if (!profile.known()) {
    return std::nullopt;
  }
  ArticulationTable table;
  if (profile.version3OrLater()) {
    if (!reader.has(offset, 0x40)) {
      return std::nullopt;
    }
    table.sampleSetId = reader.le16(offset + 4);
    table.sampleSectionSize = reader.le32(offset + 0x14);
    table.firstArtId = reader.le32(offset + 0x18);
    table.artCount = reader.le32(offset + 0x1c);
    table.artSize = profile.artRowSize();
    table.artsOffset = offset + 0x40;
  } else if (profile.hasLegacySampleHeader()) {
    if (!reader.has(offset, 0x40)) {
      return std::nullopt;
    }
    table.sampleSectionSize = reader.le32(offset + 0x14);
    table.firstArtId = reader.le32(offset + 0x18);
    const u32 endingArtId = profile.legacySampleEndingArtId(reader, offset);
    if (endingArtId < table.firstArtId) {
      return std::nullopt;
    }
    table.artCount = endingArtId - table.firstArtId;
    table.artSize = 0x40;
    table.artsOffset = offset + 0x40;
  } else {
    return std::nullopt;
  }

  if (table.artCount == 0 || table.artCount > 300 || !reader.has(table.artsOffset, table.artSize * table.artCount)) {
    return std::nullopt;
  }
  table.sampleSectionOffset = table.artsOffset + table.artSize * table.artCount;
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

[[nodiscard]] ArticulationTable hardcodedSampleHeader(ByteReader reader, AkaoInstrDatLocation location) {
  ArticulationTable table{
      .artsOffset = location.instrDatOffset,
      .artSize = 0x40,
      .sampleSectionOffset = location.instrAllOffset + 0x10,
      .sampleSectionSize = reader.has(location.instrAllOffset + 4, 4) ? reader.le32(location.instrAllOffset + 4) : 0,
      .firstArtId = location.firstArtId,
      .artCount = location.artCount,
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

[[nodiscard]] std::optional<AkaoArt> readArt(ByteReader reader, const ArticulationTable& table, AkaoPs1Version version,
                                             u32 index, u32 spuDestAddress) {
  const AkaoProfile profile = akaoProfile(version);
  const u32 artOffset = table.artsOffset + index * table.artSize;
  AkaoArt art{
      .artId = table.firstArtId + index,
      .range = reader.range(artOffset, table.artSize),
  };
  if (profile.hasCompactArtRows()) {
    const s16 rawFineTune = leS16(reader, artOffset + 8);
    const double multiplier =
        rawFineTune >= 0 ? 1.0 + (rawFineTune / 32768.0) : (static_cast<u16>(rawFineTune) / 65536.0);
    const double cents = log2Cents(multiplier);
    const s8 coarse = coarseTuneFromCents(cents);
    art.sampleOffset = reader.le32(artOffset);
    art.loopPoint = reader.le32(artOffset + 4) - art.sampleOffset;
    art.fineTuneCents = fineTuneFromCents(cents);
    art.unityKey = static_cast<u8>(reader.le16(artOffset + 0x0a) - coarse);
    art.adsr1 = reader.le16(artOffset + 0x0c);
    art.adsr2 = reader.le16(artOffset + 0x0e);
    return art;
  }

  if (version == AkaoPs1Version::Version3_0) {
    const double cents = log2Cents(reader.le32(artOffset + 8) / static_cast<double>(4096 * 256));
    const s8 coarse = coarseTuneFromCents(cents);
    art.sampleOffset = reader.le32(artOffset);
    art.loopPoint = reader.le32(artOffset + 4) - art.sampleOffset;
    art.fineTuneCents = fineTuneFromCents(cents);
    art.unityKey = static_cast<u8>(72 - coarse);
    art.adsr1 = composePsxAdsr1((reader.u8At(artOffset + 0x3d) & 4) >> 2, reader.u8At(artOffset + 0x38),
                                reader.u8At(artOffset + 0x39), reader.u8At(artOffset + 0x3a));
    art.adsr2 = composePsxAdsr2((reader.u8At(artOffset + 0x3e) & 4) >> 2, (reader.u8At(artOffset + 0x3e) & 2) >> 1,
                                reader.u8At(artOffset + 0x3b), (reader.u8At(artOffset + 0x3f) & 4) >> 2,
                                reader.u8At(artOffset + 0x3c));
    return art;
  }

  const u32 sampleStartAddress = reader.le32(artOffset);
  const u32 loopStartAddress = reader.le32(artOffset + 4);
  if (sampleStartAddress < spuDestAddress || loopStartAddress < spuDestAddress ||
      sampleStartAddress > loopStartAddress) {
    return std::nullopt;
  }
  const double cents = log2Cents(reader.le32(artOffset + 0x10) / 4096.0);
  const s8 coarse = coarseTuneFromCents(cents);
  art.sampleOffset = sampleStartAddress - spuDestAddress;
  art.loopPoint = loopStartAddress - sampleStartAddress;
  art.fineTuneCents = fineTuneFromCents(cents);
  art.unityKey = static_cast<u8>(72 - coarse);
  art.adsr1 = composePsxAdsr1((reader.u8At(artOffset + 0x0d) & 4) >> 2, reader.u8At(artOffset + 8),
                              reader.u8At(artOffset + 9), reader.u8At(artOffset + 0x0a));
  art.adsr2 = composePsxAdsr2((reader.u8At(artOffset + 0x0e) & 4) >> 2, (reader.u8At(artOffset + 0x0e) & 2) >> 1,
                              reader.u8At(artOffset + 0x0b), (reader.u8At(artOffset + 0x0f) & 4) >> 2,
                              reader.u8At(artOffset + 0x0c));
  return art;
}

[[nodiscard]] std::optional<ParsedSampleCollection> parseSampleCollectionWithTable(
    const ScanInput& input, ScanSampleCollectionRef ref, u32 offset, u32 length, AkaoPs1Version version,
    ArticulationTable table, std::string name) {
  if (table.artCount == 0 || table.sampleSectionSize == 0) {
    return std::nullopt;
  }
  const u32 spuDestAddress = akaoProfile(version).spuDestinationAddress(input.reader, offset);
  std::vector<AkaoArt> arts;
  arts.reserve(table.artCount);
  for (u32 i = 0; i < table.artCount; ++i) {
    if (auto art = readArt(input.reader, table, version, i, spuDestAddress)) {
      arts.push_back(*art);
    }
  }
  if (arts.empty()) {
    return std::nullopt;
  }

  std::set<u32> sampleOffsets;
  for (const auto& art : arts) {
    sampleOffsets.insert(art.sampleOffset);
  }

  SampleCollection collection;
  std::map<u32, u32> sampleIndexByOffset;
  std::map<u32, u32> encodedLengthByOffset;
  const u32 sampleSectionEnd = table.sampleSectionOffset + table.sampleSectionSize;
  for (const u32 sampleOffset : sampleOffsets) {
    const u32 sampleAddress = table.sampleSectionOffset + sampleOffset;
    if (sampleAddress >= sampleSectionEnd || !input.reader.has(sampleAddress, 1)) {
      continue;
    }
    const PsxSampleInfo sampleInfo = psxSampleInfo(input.reader, sampleAddress, sampleSectionEnd);
    const u32 encodedLength = sampleInfo.encodedLength;
    if (encodedLength == 0) {
      continue;
    }
    const u32 sampleIndex = static_cast<u32>(collection.samples.size());
    sampleIndexByOffset.emplace(sampleOffset, sampleIndex);
    encodedLengthByOffset.emplace(sampleOffset, encodedLength);
    collection.samples.push_back(Sample{
        .name = fmt::format("Sample {}", sampleIndex),
        .codec = AudioCodec::PsxAdpcm,
        .encodedData = input.reader.range(sampleAddress, encodedLength),
        .sampleRate = kPsxSampleRate,
        .channels = 1,
        .bitsPerSample = 16,
        .loop = sampleInfo.loop,
    });
  }
  if (collection.samples.empty()) {
    return std::nullopt;
  }

  for (auto& art : arts) {
    if (auto found = sampleIndexByOffset.find(art.sampleOffset); found != sampleIndexByOffset.end()) {
      art.sampleIndex = found->second;
      const u32 encodedLength = encodedLengthByOffset.find(art.sampleOffset)->second;
      Loop& sampleLoop = collection.samples[art.sampleIndex].loop;
      if (sampleLoop.enabled && sampleLoop.start == 0 && sampleLoop.length == 0) {
        const u32 loopStart = art.loopPoint < encodedLength ? psxDecodedOffset(art.loopPoint) : 0;
        art.loop = Loop{
            .enabled = true,
            .start = loopStart,
            .length = loopStart < psxDecodedFrames(encodedLength) ? psxDecodedFrames(encodedLength) - loopStart : 0,
        };
      } else if (!sampleLoop.enabled && sampleLoop.start == 0 && sampleLoop.length == 0 && art.loopPoint != 0 &&
                 art.loopPoint < encodedLength) {
        const u32 loopStart = psxDecodedOffset(art.loopPoint);
        sampleLoop.start = loopStart;
        sampleLoop.length =
            loopStart < psxDecodedFrames(encodedLength) ? psxDecodedFrames(encodedLength) - loopStart : 0;
      }
    }
  }

  const SourceRange range = input.reader.range(offset, length);
  return ParsedSampleCollection{
      .parse =
          AkaoSampleCollectionParse{
              .ref = ref,
              .sampleSetId = table.sampleSetId,
              .offset = offset,
              .length = length,
              .version = version,
              .firstArtId = table.firstArtId,
              .artCount = table.artCount,
              .arts = std::move(arts),
          },
      .samples = std::move(collection),
      .name = std::move(name),
      .range = range,
      .table = table,
  };
}

void emitSampleCollection(const ScanInput& input, ScanResultBuilder& result, ScanSampleCollectionRef ref,
                          ParsedSampleCollection& parsed) {
  const SourceAnnotationId root = result.sourceMap()
                                      .annotation(SourceRole::SampleCollection, parsed.name, parsed.range)
                                      .kind("akao-sample-collection")
                                      .owner(ObjectRefs::asset(ref.id))
                                      .id();
  for (u32 i = 0; i < parsed.samples.samples.size(); ++i) {
    result.sourceMap()
        .annotation(SourceRole::Sample, parsed.samples.samples[i].name, parsed.samples.samples[i].encodedData)
        .kind("psx-adpcm-sample")
        .owner(ObjectRefs::sample(ref.id, i))
        .parent(root);
  }
  const SourceRange artTableRange =
      input.reader.range(parsed.table.artsOffset, parsed.table.artSize * parsed.table.artCount);
  const SourceAnnotationId artTable =
      result.sourceMap()
          .table("Akao Articulation Table", artTableRange)
          .kind("akao-articulation-table")
          .parent(root)
          .derived("first_art_id", parsed.table.firstArtId)
          .derived("art_count", parsed.table.artCount)
          .id();
  for (const AkaoArt& art : parsed.parse.arts) {
    auto annotation =
        result.sourceMap()
            .entry(fmt::format("Articulation {}", art.artId), art.range)
            .kind("akao-articulation")
            .parent(artTable)
            .derived("art_id", art.artId)
            .derived("unity_key", art.unityKey, SourceValueDisplay::MidiNote)
            .derived("fine_tune_cents", art.fineTuneCents, SourceValueDisplay::Cents)
            .derived("sample_offset", art.sampleOffset, SourceValueDisplay::Address)
            .derived("loop_point", art.loopPoint, SourceValueDisplay::Address)
            .derived("adsr1", art.adsr1, SourceValueDisplay::Hex)
            .derived("adsr2", art.adsr2, SourceValueDisplay::Hex);
    if (art.sampleIndex < parsed.samples.samples.size()) {
      annotation.link(SourceLinkRole::UsesSample, SourceTarget{ObjectRefs::sample(ref.id, art.sampleIndex)});
    }
  }

  result.sampleCollection(ref, [&](AssetId id) {
    return SampleCollectionAsset{
        .metadata =
            AssetMetadata{
                .id = id,
                .format = std::string(kAkaoFormatName),
                .name = parsed.name,
                .range = parsed.range,
            },
        .samples = std::move(parsed.samples),
    };
  });
}

[[nodiscard]] std::optional<ParsedSampleCollection> parseAkaoSampleCollectionBuild(
    const ScanInput& input, ScanSampleCollectionRef ref, u32 offset, AkaoPs1Version version) {
  if (version == AkaoPs1Version::Unknown) {
    version = guessSampleVersion(input.reader, offset);
  }
  auto table = sampleHeader(input.reader, offset, version);
  if (!table) {
    return std::nullopt;
  }
  const u32 length = static_cast<u32>(
      std::min<u64>(input.reader.size() - offset, table->sampleSectionOffset + table->sampleSectionSize - offset));
  return parseSampleCollectionWithTable(
      input, ref, offset, length, version, *table,
      fmt::format("Akao Sample Collection {:02X}", table->sampleSetId.value_or(input.reader.le16(offset + 4))));
}

[[nodiscard]] std::optional<ParsedSampleCollection> parseAkaoSampleCollectionBuild(
    const ScanInput& input, ScanSampleCollectionRef ref, AkaoInstrDatLocation location) {
  if (!input.reader.has(location.instrAllOffset, 8) || !input.reader.has(location.instrDatOffset, 1)) {
    return std::nullopt;
  }
  auto table = hardcodedSampleHeader(input.reader, location);
  if (!input.reader.has(table.artsOffset, table.artSize * table.artCount) || table.sampleSectionSize == 0) {
    return std::nullopt;
  }
  const u32 offset = std::min(location.instrAllOffset, location.instrDatOffset);
  const u32 endOffset =
      std::max(table.sampleSectionOffset + table.sampleSectionSize, table.artsOffset + table.artSize * table.artCount);
  return parseSampleCollectionWithTable(input, ref, offset, endOffset - offset, AkaoPs1Version::Version1_0, table,
                                        "Akao Sample Collection FF7");
}

}  // namespace

std::optional<AkaoInstrDatLocation> ff7HardcodedAkaoSampleLocation(ByteReader reader) {
  if (reader.size() >= 0x1a8000 && reader.has(0xe0000, 4) && reader.has(0x156000, 4) &&
      reader.le32(0xe0000) == 0x1010 && reader.le32(0x156000) == 0x1010) {
    return AkaoInstrDatLocation{
        .instrAllOffset = 0xe0000,
        .instrDatOffset = 0x156000,
        .firstArtId = 0,
        .artCount = 128,
    };
  }
  return std::nullopt;
}

bool isPossibleAkaoSampleCollection(ByteReader reader, u32 offset) {
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

std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollectionData(const ScanInput& input,
                                                                       ScanSampleCollectionRef ref, u32 offset,
                                                                       AkaoPs1Version version) {
  auto parsed = parseAkaoSampleCollectionBuild(input, ref, offset, version);
  if (!parsed) {
    return std::nullopt;
  }
  return std::move(parsed->parse);
}

std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollectionData(const ScanInput& input,
                                                                       ScanSampleCollectionRef ref,
                                                                       AkaoInstrDatLocation location) {
  auto parsed = parseAkaoSampleCollectionBuild(input, ref, location);
  if (!parsed) {
    return std::nullopt;
  }
  return std::move(parsed->parse);
}

std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const ScanInput& input, ScanResultBuilder& result,
                                                                   ScanSampleCollectionRef ref, u32 offset,
                                                                   AkaoPs1Version version) {
  auto parsed = parseAkaoSampleCollectionBuild(input, ref, offset, version);
  if (!parsed) {
    return std::nullopt;
  }
  emitSampleCollection(input, result, ref, *parsed);
  return std::move(parsed->parse);
}

std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const ScanInput& input, ScanResultBuilder& result,
                                                                   ScanSampleCollectionRef ref,
                                                                   AkaoInstrDatLocation location) {
  auto parsed = parseAkaoSampleCollectionBuild(input, ref, location);
  if (!parsed) {
    return std::nullopt;
  }
  emitSampleCollection(input, result, ref, *parsed);
  return std::move(parsed->parse);
}

}  // namespace vgmtrans::formats::akao

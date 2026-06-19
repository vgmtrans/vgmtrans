/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoSynth.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

constexpr u32 kAkaoSignature = 0x414B414F;
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

[[nodiscard]] s16 leS16(ByteReader reader, u32 offset) {
  return static_cast<s16>(reader.le16(offset));
}

[[nodiscard]] u16 composePsxAdsr1(u8 attackMode, u8 attackRate, u8 decayRate, u8 sustainLevel) {
  return static_cast<u16>(((attackMode & 1) << 15) | ((attackRate & 0x7f) << 8) | ((decayRate & 0x0f) << 4) |
                          (sustainLevel & 0x0f));
}

[[nodiscard]] u16 composePsxAdsr2(u8 sustainMode, u8 sustainDirection, u8 sustainRate, u8 releaseMode,
                                  u8 releaseRate) {
  return static_cast<u16>(((sustainMode & 1) << 15) | ((sustainDirection & 1) << 14) |
                          ((sustainRate & 0x7f) << 6) | ((releaseMode & 1) << 5) | (releaseRate & 0x1f));
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

[[nodiscard]] double attenuationDbFromLinear(double linear) {
  if (linear <= 0.0) {
    return 96.0;
  }
  return -20.0 * std::log10(std::min(1.0, linear));
}

[[nodiscard]] Envelope psxEnvelope(u16 adsr1, u16 adsr2) {
  const u8 sustainLevel = adsr1 & 0x0f;
  const u8 releaseRate = adsr2 & 0x1f;
  const double sustain = sustainLevel == 0 ? (0x07ffffff / static_cast<double>(0x7fffffff))
                                           : ((sustainLevel + 1) / 16.0);
  return Envelope{
      .sustain = static_cast<u32>(std::round(sustain * 1000.0)),
      .release = static_cast<u32>((0x1f - releaseRate) * 40000),
      .releaseSeconds = (0x1f - releaseRate) * 0.04,
      .sustainAmplitude = sustain,
  };
}

[[nodiscard]] Envelope akaoRegionEnvelope(const AkaoArt& art, u8 attackRate, u8 sustainRate, u8 sustainMode,
                                          u8 releaseRate) {
  u16 adsr1 = art.adsr1;
  u16 adsr2 = art.adsr2;
  // Legacy Akao applies region-level ADSR bytes over the articulation ADSR. Keep that
  // behavior here so key-split and drum regions retain their per-region shaping.
  adsr1 &= static_cast<u16>(~0x7f00u);
  adsr1 |= static_cast<u16>((attackRate & 0x7f) << 8);
  adsr2 &= static_cast<u16>(~0xffdfu);
  adsr2 |= static_cast<u16>((sustainRate & 0x7f) << 6);
  adsr2 |= static_cast<u16>((sustainMode & 0x07) << 13);
  adsr2 |= static_cast<u16>(releaseRate & 0x1f);
  return psxEnvelope(adsr1, adsr2);
}

[[nodiscard]] u32 psxDecodedFrames(u32 encodedBytes) {
  return (encodedBytes / kPsxAdpcmBlockBytes) * kPsxAdpcmBlockSamples;
}

[[nodiscard]] u32 psxDecodedOffset(u32 encodedOffset) {
  return (encodedOffset / kPsxAdpcmBlockBytes) * kPsxAdpcmBlockSamples;
}

[[nodiscard]] u32 psxSampleLength(ByteReader reader, u32 offset, u32 endOffset, bool& loops) {
  u32 cursor = offset;
  loops = false;
  while (cursor + kPsxAdpcmBlockBytes <= endOffset && reader.has(cursor, kPsxAdpcmBlockBytes)) {
    const u8 flags = reader.u8At(cursor + 1);
    cursor += kPsxAdpcmBlockBytes;
    if ((flags & 1) != 0) {
      loops = (flags & 2) != 0;
      return cursor - offset;
    }
  }
  return cursor > offset ? cursor - offset : 0;
}

[[nodiscard]] std::optional<ArticulationTable> sampleHeader(ByteReader reader, u32 offset, AkaoPs1Version version) {
  if (version == AkaoPs1Version::Unknown) {
    return std::nullopt;
  }
  ArticulationTable table;
  if (isVersion3OrLater(version)) {
    if (!reader.has(offset, 0x40)) {
      return std::nullopt;
    }
    table.sampleSetId = reader.le16(offset + 4);
    table.sampleSectionSize = reader.le32(offset + 0x14);
    table.firstArtId = reader.le32(offset + 0x18);
    table.artCount = reader.le32(offset + 0x1c);
    table.artSize = version >= AkaoPs1Version::Version3_1 ? 0x10 : 0x40;
    table.artsOffset = offset + 0x40;
  } else if (version >= AkaoPs1Version::Version1_1) {
    if (!reader.has(offset, 0x40)) {
      return std::nullopt;
    }
    table.sampleSectionSize = reader.le32(offset + 0x14);
    table.firstArtId = reader.le32(offset + 0x18);
    const u32 endingArtId =
        version == AkaoPs1Version::Version1_1 ? 0x80 : (reader.le32(offset + 0x1c) == 0 ? 0x100 : reader.le32(offset + 0x1c));
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
  const u32 artOffset = table.artsOffset + index * table.artSize;
  AkaoArt art{.artId = table.firstArtId + index};
  if (version >= AkaoPs1Version::Version3_1) {
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
    art.adsr2 = composePsxAdsr2((reader.u8At(artOffset + 0x3e) & 4) >> 2,
                                (reader.u8At(artOffset + 0x3e) & 2) >> 1, reader.u8At(artOffset + 0x3b),
                                (reader.u8At(artOffset + 0x3f) & 4) >> 2, reader.u8At(artOffset + 0x3c));
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
  art.adsr2 = composePsxAdsr2((reader.u8At(artOffset + 0x0e) & 4) >> 2,
                              (reader.u8At(artOffset + 0x0e) & 2) >> 1, reader.u8At(artOffset + 0x0b),
                              (reader.u8At(artOffset + 0x0f) & 4) >> 2, reader.u8At(artOffset + 0x0c));
  return art;
}

[[nodiscard]] std::optional<AkaoSampleCollectionParse>
parseSampleCollectionWithTable(const ScanInput& input, ScanResultBuilder& result, ScanSampleCollectionRef ref,
                               u32 offset, u32 length, AkaoPs1Version version, ArticulationTable table,
                               u32 scanOrdinal, std::string name) {
  if (table.artCount == 0 || table.sampleSectionSize == 0) {
    return std::nullopt;
  }
  const u32 spuDestAddress = version == AkaoPs1Version::Version1_0
                                 ? (input.reader.has(offset, 4) ? input.reader.le32(offset) : 0)
                                 : (input.reader.has(offset + 0x10, 4) ? input.reader.le32(offset + 0x10) : 0);
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
  const u32 sampleSectionEnd = table.sampleSectionOffset + table.sampleSectionSize;
  for (const u32 sampleOffset : sampleOffsets) {
    const u32 sampleAddress = table.sampleSectionOffset + sampleOffset;
    if (sampleAddress >= sampleSectionEnd || !input.reader.has(sampleAddress, 1)) {
      continue;
    }
    bool loops = false;
    const u32 encodedLength = psxSampleLength(input.reader, sampleAddress, sampleSectionEnd, loops);
    if (encodedLength == 0) {
      continue;
    }
    const u32 sampleIndex = static_cast<u32>(collection.samples.size());
    sampleIndexByOffset.emplace(sampleOffset, sampleIndex);
    auto loopArt = std::ranges::find_if(arts, [sampleOffset](const AkaoArt& art) {
      return art.sampleOffset == sampleOffset && art.loopPoint != 0;
    });
    const u32 decodedLength = psxDecodedFrames(encodedLength);
    const u32 loopStart = loopArt != arts.end() ? psxDecodedOffset(loopArt->loopPoint) : 0;
    collection.samples.push_back(Sample{
        .name = fmt::format("Sample {}", sampleIndex),
        .codec = AudioCodec::PsxAdpcm,
        .encodedData = input.reader.range(sampleAddress, encodedLength),
        .sampleRate = kPsxSampleRate,
        .channels = 1,
        .bitsPerSample = 16,
        .loop =
            Loop{
                .enabled = loops && loopStart < decodedLength,
                .start = loopStart,
                .length = loopStart < decodedLength ? decodedLength - loopStart : 0,
            },
    });
  }
  if (collection.samples.empty()) {
    return std::nullopt;
  }

  for (auto& art : arts) {
    if (auto found = sampleIndexByOffset.find(art.sampleOffset); found != sampleIndexByOffset.end()) {
      art.sampleIndex = found->second;
    }
  }

  ItemTree items;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const SourceRange range = input.reader.range(offset, length);
  const ItemId root = itemBuilder.add(std::nullopt, ItemKind::SampleCollection, "akao-sample-collection", name, range);
  for (u32 i = 0; i < collection.samples.size(); ++i) {
    static_cast<void>(itemBuilder.add(root, ItemKind::Sample, "psx-adpcm-sample", collection.samples[i].name,
                                      collection.samples[i].encodedData));
  }

  static_cast<void>(result.sampleCollection(ref, [&](AssetId id) {
    return SampleCollectionAsset{
        .metadata =
            AssetMetadata{
                .id = id,
                .format = std::string(kAkaoFormatName),
                .name = name,
                .range = range,
                .items = std::move(items),
            },
        .samples = std::move(collection),
    };
  }));

  return AkaoSampleCollectionParse{
      .ref = ref,
      .sampleSetId = table.sampleSetId,
      .offset = offset,
      .length = length,
      .version = version,
      .firstArtId = table.firstArtId,
      .artCount = table.artCount,
      .scanOrdinal = scanOrdinal,
      .arts = std::move(arts),
  };
}

void applyArtToRegion(Region& region, const AkaoArtBinding* binding, u8 attackRate, u8 sustainRate, u8 sustainMode,
                      u8 releaseRate, bool drum, u8 drumRelativeUnityKey = 0) {
  if (binding == nullptr) {
    return;
  }
  const AkaoArt& art = binding->art;
  region.sample = SampleRef{.collection = binding->collection.id, .index = binding->sampleIndex};
  region.rootKey = drum ? static_cast<u8>(art.unityKey + region.keyRange.low - drumRelativeUnityKey) : art.unityKey;
  region.fineTuneCents = art.fineTuneCents;
  region.envelope = akaoRegionEnvelope(art, attackRate, sustainRate, sustainMode, releaseRate);
}

[[nodiscard]] const AkaoArtBinding* findArt(const AkaoArtMap& artMap, u32 artId) {
  const auto found = artMap.find(artId);
  return found == artMap.end() ? nullptr : &found->second;
}

[[nodiscard]] Region readMelodicRegion(ByteReader reader, u32 offset, const AkaoArtMap& artMap) {
  const u8 artId = reader.u8At(offset);
  Region region{
      .keyRange = KeyRange{.low = reader.u8At(offset + 1), .high = reader.u8At(offset + 2)},
      .velocityRange = VelocityRange{.low = 0, .high = 127},
      .sample = SampleRef{.index = 0},
      .range = reader.range(offset, 8),
      .attenuationDb = attenuationDbFromLinear(reader.u8At(offset + 7) == 0 ? 1.0 : reader.u8At(offset + 7) / 128.0),
  };
  applyArtToRegion(region, findArt(artMap, artId), reader.u8At(offset + 3), reader.u8At(offset + 4),
                   reader.u8At(offset + 5), reader.u8At(offset + 6), false);
  return region;
}

[[nodiscard]] std::vector<Region> readMelodicRegions(ByteReader reader, u32 offset, u32 endOffset,
                                                     AkaoPs1Version version, const AkaoArtMap& artMap) {
  std::vector<Region> regions;
  for (u32 regionIndex = 0; regionIndex < 128 && offset + regionIndex * 8 + 8 <= endOffset &&
                             reader.has(offset + regionIndex * 8, 8);
       ++regionIndex) {
    const u32 regionOffset = offset + regionIndex * 8;
    if (!isVersion3OrLater(version) && reader.u8At(regionOffset) >= 0x80) {
      break;
    }
    if (isVersion3OrLater(version) && reader.le32(regionOffset) == 0) {
      break;
    }
    Region region = readMelodicRegion(reader, regionOffset, artMap);
    if (!regions.empty()) {
      Region& previous = regions.back();
      if (region.keyRange.high > previous.keyRange.high && region.keyRange.low > previous.keyRange.high) {
        if (region.keyRange.low > previous.keyRange.high + 1) {
          region.keyRange.low = previous.keyRange.high + 1;
        }
        regions.push_back(std::move(region));
      } else if (region.keyRange.high != previous.keyRange.high) {
        regions.push_back(std::move(region));
      }
    } else {
      regions.push_back(std::move(region));
    }
  }
  if (!regions.empty()) {
    regions.front().keyRange.low = 0;
    regions.back().keyRange.high = 127;
  }
  return regions;
}

void addMelodicInstrument(std::vector<Instrument>& instruments, ByteReader reader, u32 offset, u32 endOffset,
                          AkaoPs1Version version, const AkaoArtMap& artMap, u32 program) {
  auto regions = readMelodicRegions(reader, offset, endOffset, version, artMap);
  if (regions.empty()) {
    return;
  }
  instruments.push_back(Instrument{
      .bank = 1,
      .program = program,
      .name = fmt::format("Instrument {}", program),
      .range = reader.range(offset, std::min<u32>(endOffset - offset, static_cast<u32>(regions.size() * 8))),
      .regions = std::move(regions),
  });
}

void addDrumInstrument(std::vector<Instrument>& instruments, ByteReader reader, u32 offset, u32 endOffset,
                       AkaoPs1Version version, const AkaoArtMap& artMap, u32 program = 127) {
  Instrument drum{
      .bank = 127,
      .program = program,
      .name = "Drum Kit",
      .range = reader.range(offset, 0),
  };
  if (isVersion3OrLater(version)) {
    for (u32 key = 0; key < 128; ++key) {
      const u32 regionOffset = offset + key * 8;
      if (regionOffset + 8 > endOffset || !reader.has(regionOffset, 8)) {
        break;
      }
      if (reader.le32(regionOffset) == 0 && reader.le32(regionOffset + 4) == 0) {
        continue;
      }
      if (reader.le32(regionOffset) == 0xffffffff && reader.le32(regionOffset + 4) == 0xffffffff) {
        break;
      }
      const u8 artId = reader.u8At(regionOffset);
      Region region{
          .keyRange = KeyRange{.low = static_cast<u8>(key), .high = static_cast<u8>(key)},
          .velocityRange = VelocityRange{.low = 0, .high = 127},
          .sample = SampleRef{.index = 0},
          .range = reader.range(regionOffset, 8),
          .pan = std::clamp(static_cast<double>(reader.u8At(regionOffset + 7) & 0x7f) / 127.0, 0.0, 1.0),
          .attenuationDb =
              attenuationDbFromLinear(reader.u8At(regionOffset + 6) == 0 ? 1.0 : reader.u8At(regionOffset + 6) / 128.0),
      };
      applyArtToRegion(region, findArt(artMap, artId), reader.u8At(regionOffset + 2), reader.u8At(regionOffset + 3),
                       reader.u8At(regionOffset + 4), reader.u8At(regionOffset + 5), true,
                       reader.u8At(regionOffset + 1));
      drum.regions.push_back(std::move(region));
    }
  } else {
    const u32 regionSize = version >= AkaoPs1Version::Version2 ? 6 : 5;
    for (u32 drumKey = 0; drumKey < 12; ++drumKey) {
      const u32 regionOffset = offset + drumKey * regionSize;
      if (regionOffset + regionSize > endOffset || !reader.has(regionOffset, regionSize)) {
        break;
      }
      if (reader.le32(regionOffset) == 0 && reader.u8At(regionOffset + 4) == 0 &&
          (version < AkaoPs1Version::Version2 || reader.u8At(regionOffset + 5) == 0)) {
        continue;
      }
      const u8 artId = reader.u8At(regionOffset);
      const u8 key = static_cast<u8>(24 + drumKey);
      Region region{
          .keyRange = KeyRange{.low = key, .high = key},
          .velocityRange = VelocityRange{.low = 0, .high = 127},
          .sample = SampleRef{.index = 0},
          .range = reader.range(regionOffset, regionSize),
          .pan = std::clamp(static_cast<double>(reader.u8At(regionOffset + 4)) / 127.0, 0.0, 1.0),
          .attenuationDb = attenuationDbFromLinear(reader.le16(regionOffset + 2) / (127.0 * 128.0)),
      };
      applyArtToRegion(region, findArt(artMap, artId), 0, 0, 0, 0, true, reader.u8At(regionOffset + 1));
      drum.regions.push_back(std::move(region));
    }
  }
  if (!drum.regions.empty()) {
    drum.range.size = static_cast<u32>(drum.regions.back().range.offset + drum.regions.back().range.size - offset);
    instruments.push_back(std::move(drum));
  }
}

void addSyntheticArtInstruments(std::vector<Instrument>& instruments, const AkaoArtMap& artMap) {
  for (const auto& [artId, binding] : artMap) {
    Region region{
        .keyRange = KeyRange{.low = 0, .high = 127},
        .velocityRange = VelocityRange{.low = 0, .high = 127},
        .sample = SampleRef{.collection = binding.collection.id, .index = binding.sampleIndex},
        .rootKey = binding.art.unityKey,
        .fineTuneCents = binding.art.fineTuneCents,
        .envelope = psxEnvelope(binding.art.adsr1, binding.art.adsr2),
    };
    instruments.push_back(Instrument{
        .bank = 0,
        .program = artId,
        .name = fmt::format("Articulation {}", artId),
        .regions = {std::move(region)},
    });
  }
}

}  // namespace

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

std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const ScanInput& input, ScanResultBuilder& result,
                                                                   ScanSampleCollectionRef ref, u32 offset,
                                                                   AkaoPs1Version version, u32 scanOrdinal) {
  if (version == AkaoPs1Version::Unknown) {
    version = guessSampleVersion(input.reader, offset);
  }
  auto table = sampleHeader(input.reader, offset, version);
  if (!table) {
    return std::nullopt;
  }
  const u32 length = static_cast<u32>(std::min<u64>(
      input.reader.size() - offset, table->sampleSectionOffset + table->sampleSectionSize - offset));
  return parseSampleCollectionWithTable(input, result, ref, offset, length, version, *table, scanOrdinal,
                                        fmt::format("Akao Sample Collection {:02X}",
                                                    table->sampleSetId.value_or(input.reader.le16(offset + 4))));
}

std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const ScanInput& input, ScanResultBuilder& result,
                                                                   ScanSampleCollectionRef ref,
                                                                   AkaoInstrDatLocation location, u32 scanOrdinal) {
  if (!input.reader.has(location.instrAllOffset, 8) || !input.reader.has(location.instrDatOffset, 1)) {
    return std::nullopt;
  }
  auto table = hardcodedSampleHeader(input.reader, location);
  if (!input.reader.has(table.artsOffset, table.artSize * table.artCount) || table.sampleSectionSize == 0) {
    return std::nullopt;
  }
  const u32 offset = std::min(location.instrAllOffset, location.instrDatOffset);
  const u32 endOffset = std::max(table.sampleSectionOffset + table.sampleSectionSize, table.artsOffset + table.artSize * table.artCount);
  return parseSampleCollectionWithTable(input, result, ref, offset, endOffset - offset, AkaoPs1Version::Version1_0,
                                        table, scanOrdinal, "Akao Sample Collection FF7");
}

AkaoArtMap buildAkaoArtMap(const std::vector<AkaoSampleCollectionParse>& sampleCollections) {
  AkaoArtMap map;
  for (const auto& collection : sampleCollections) {
    for (const auto& art : collection.arts) {
      map[art.artId] = AkaoArtBinding{
          .collection = collection.ref,
          .sampleIndex = art.sampleIndex,
          .art = art,
      };
    }
  }
  return map;
}

InstrumentSetAsset parseAkaoInstrumentSet(const ScanInput& input, AssetId id, const AkaoSequenceAnalysis& sequence,
                                          const AkaoArtMap& artMap) {
  std::vector<Instrument> instruments;
  const u32 sequenceEnd = sequence.header.offset + sequence.header.length;
  if (sequence.header.instrumentSetOffset) {
    const u32 instrSetOffset = *sequence.header.instrumentSetOffset;
    for (u32 program = 0; program < 16 && input.reader.has(instrSetOffset + program * 2, 2); ++program) {
      const u16 pointer = input.reader.le16(instrSetOffset + program * 2);
      if (pointer == 0xffff || (pointer == 0 && program != 0)) {
        continue;
      }
      const u32 instrOffset = instrSetOffset + 0x20 + pointer;
      if (instrOffset < sequenceEnd && input.reader.has(instrOffset, 8)) {
        addMelodicInstrument(instruments, input.reader, instrOffset, sequenceEnd, sequence.header.version, artMap,
                             program);
      }
    }
  } else {
    u32 program = 0;
    for (const u32 instrOffset : sequence.customInstrumentOffsets) {
      if (instrOffset < sequenceEnd && input.reader.has(instrOffset, 8)) {
        addMelodicInstrument(instruments, input.reader, instrOffset, sequenceEnd, sequence.header.version, artMap,
                             program++);
      }
    }
  }

  if (sequence.header.drumSetOffset && *sequence.header.drumSetOffset < sequenceEnd) {
    addDrumInstrument(instruments, input.reader, *sequence.header.drumSetOffset, sequenceEnd, sequence.header.version,
                      artMap);
  } else {
    u32 drumIndex = 0;
    for (const u32 drumOffset : sequence.drumInstrumentOffsets) {
      if (drumOffset < sequenceEnd && input.reader.has(drumOffset, 5)) {
        addDrumInstrument(instruments, input.reader, drumOffset, sequenceEnd, sequence.header.version, artMap,
                          127 - drumIndex++);
      }
    }
  }

  if (sequence.usesIndividualArts) {
    addSyntheticArtInstruments(instruments, artMap);
  }

  ItemTree items;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const std::string name = fmt::format("Akao Instr Set {:02X}", sequence.header.sequenceId);
  const SourceRange range = input.reader.range(sequence.header.instrumentSetOffset.value_or(
      sequence.header.drumSetOffset.value_or(sequence.header.offset)), 0);
  const ItemId root = itemBuilder.add(std::nullopt, ItemKind::InstrumentSet, "akao-instrument-set", name, range);
  for (u32 i = 0; i < instruments.size(); ++i) {
    static_cast<void>(
        itemBuilder.add(root, ItemKind::Instrument, "akao-instrument", instruments[i].name, instruments[i].range));
  }

  return InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kAkaoFormatName),
              .name = name,
              .range = range,
              .items = std::move(items),
          },
      .instruments = std::move(instruments),
  };
}

std::vector<u32> requiredArticulations(ByteReader reader, const AkaoSequenceAnalysis& sequence) {
  std::set<u32> required;
  const u32 sequenceEnd = sequence.header.offset + sequence.header.length;
  const auto addMelodic = [&](u32 offset) {
    for (u32 regionIndex = 0; regionIndex < 128 && offset + regionIndex * 8 + 8 <= sequenceEnd &&
                               reader.has(offset + regionIndex * 8, 8);
         ++regionIndex) {
      const u32 regionOffset = offset + regionIndex * 8;
      if (!isVersion3OrLater(sequence.header.version) && reader.u8At(regionOffset) >= 0x80) {
        break;
      }
      if (isVersion3OrLater(sequence.header.version) && reader.le32(regionOffset) == 0) {
        break;
      }
      const u8 artId = reader.u8At(regionOffset);
      if (artId != 0) {
        required.insert(artId);
      }
    }
  };
  const auto addDrum = [&](u32 offset) {
    if (isVersion3OrLater(sequence.header.version)) {
      for (u32 key = 0; key < 128; ++key) {
        const u32 regionOffset = offset + key * 8;
        if (regionOffset + 8 > sequenceEnd || !reader.has(regionOffset, 8)) {
          break;
        }
        if (reader.le32(regionOffset) == 0 && reader.le32(regionOffset + 4) == 0) {
          continue;
        }
        if (reader.le32(regionOffset) == 0xffffffff && reader.le32(regionOffset + 4) == 0xffffffff) {
          break;
        }
        const u8 artId = reader.u8At(regionOffset);
        if (artId != 0) {
          required.insert(artId);
        }
      }
      return;
    }
    const u32 regionSize = sequence.header.version >= AkaoPs1Version::Version2 ? 6 : 5;
    for (u32 drumKey = 0; drumKey < 12; ++drumKey) {
      const u32 regionOffset = offset + drumKey * regionSize;
      if (regionOffset + regionSize > sequenceEnd || !reader.has(regionOffset, regionSize)) {
        break;
      }
      if (reader.le32(regionOffset) == 0 && reader.u8At(regionOffset + 4) == 0 &&
          (sequence.header.version < AkaoPs1Version::Version2 || reader.u8At(regionOffset + 5) == 0)) {
        continue;
      }
      const u8 artId = reader.u8At(regionOffset);
      if (artId != 0) {
        required.insert(artId);
      }
    }
  };

  if (sequence.header.instrumentSetOffset) {
    const u32 instrSetOffset = *sequence.header.instrumentSetOffset;
    for (u32 program = 0; program < 16 && reader.has(instrSetOffset + program * 2, 2); ++program) {
      const u16 pointer = reader.le16(instrSetOffset + program * 2);
      if (pointer == 0xffff || (pointer == 0 && program != 0)) {
        continue;
      }
      const u32 instrOffset = instrSetOffset + 0x20 + pointer;
      if (instrOffset < sequenceEnd && reader.has(instrOffset, 8)) {
        addMelodic(instrOffset);
      }
    }
  } else {
    for (const u32 instrOffset : sequence.customInstrumentOffsets) {
      if (instrOffset < sequenceEnd && reader.has(instrOffset, 8)) {
        addMelodic(instrOffset);
      }
    }
  }

  if (sequence.header.drumSetOffset && *sequence.header.drumSetOffset < sequenceEnd) {
    addDrum(*sequence.header.drumSetOffset);
  } else {
    for (const u32 drumOffset : sequence.drumInstrumentOffsets) {
      if (drumOffset < sequenceEnd && reader.has(drumOffset, 5)) {
        addDrum(drumOffset);
      }
    }
  }
  if (!sequence.header.sampleSetId || *sequence.header.sampleSetId == 0) {
    required.insert(sequence.individualArtIds.begin(), sequence.individualArtIds.end());
  }
  return {required.begin(), required.end()};
}

}  // namespace vgmtrans::formats::akao

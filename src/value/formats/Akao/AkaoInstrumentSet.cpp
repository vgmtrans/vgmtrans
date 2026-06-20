/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoSynth.h"

#include "value/formats/Akao/AkaoVersion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

[[nodiscard]] double attenuationDbFromLinear(double linear) {
  if (linear <= 0.0) {
    return 96.0;
  }
  return -20.0 * std::log10(std::min(1.0, linear));
}

[[nodiscard]] double regionPanFromRaw(u8 pan) {
  if (pan == 127) {
    return 1.0;
  }
  if (pan == 0) {
    return 0.0;
  }
  if (pan == 64) {
    return 0.5;
  }
  return pan / 127.0;
}

[[nodiscard]] int roundToZero(int value) {
  return value < 0 ? 0 : value;
}

[[nodiscard]] const std::array<unsigned long, 160>& psxRateTable() {
  static const std::array<unsigned long, 160> table = [] {
    std::array<unsigned long, 160> rates{};
    u32 rate = 3;
    u32 step = 1;
    u32 divider = 0;
    for (int i = 32; i < 160; ++i) {
      if (rate < 0x3fffffffu) {
        rate += step;
        ++divider;
        if (divider == 5) {
          divider = 1;
          step *= 2;
        }
      }
      if (rate > 0x3fffffffu) {
        rate = 0x3fffffffu;
      }
      rates[static_cast<size_t>(i)] = rate;
    }
    return rates;
  }();
  return table;
}

// Both SF2 and DLS define decay/release as a constant rate of dB attenuation.
// PSX SPU linear modes change amplitude at a constant rate, so legacy VGMTrans
// expands those times to keep the perceived fade shape closer after conversion.
[[nodiscard]] double linearAmplitudeDecayToDbDecay(double secondsToFullAttenuation) {
  if (secondsToFullAttenuation <= 0.0) {
    return 0.0;
  }

  constexpr double targetDbLeastSquares = 70.0;
  constexpr double targetDbInitialSlope = 140.0;
  constexpr double ln10 = 2.302585092994046;
  constexpr double kneeSeconds = 0.12;
  constexpr double kneePower = 2.0;

  const double shortScale = targetDbInitialSlope / (20.0 / ln10);
  const double longScale = targetDbLeastSquares * ln10 / 45.0;
  const double x = secondsToFullAttenuation / kneeSeconds;
  const double weight = 1.0 / (1.0 + std::pow(x, kneePower));
  return secondsToFullAttenuation * (weight * shortScale + (1.0 - weight) * longScale);
}

[[nodiscard]] Envelope psxEnvelope(u16 adsr1, u16 adsr2) {
  u8 attackMode = (adsr1 & 0x8000) >> 15;
  u8 attackRate = (adsr1 & 0x7f00) >> 8;
  u8 decayRate = (adsr1 & 0x00f0) >> 4;
  const u8 sustainLevel = adsr1 & 0x000f;
  const u8 sustainMode = (adsr2 & 0x8000) >> 15;
  const u8 sustainDirection = (adsr2 & 0x4000) >> 14;
  const u8 sustainRate = (adsr2 >> 6) & 0x7f;
  u8 releaseMode = (adsr2 & 0x0020) >> 5;
  u8 releaseRate = adsr2 & 0x001f;
  const auto& rates = psxRateTable();
  constexpr double sampleRate = 44100.0;

  double samples = 0.0;
  if ((attackRate ^ 0x7f) < 0x10) {
    attackRate = 0;
  }
  if (attackMode == 0) {
    const u32 rate = rates[roundToZero((attackRate ^ 0x7f) - 0x10) + 32];
    samples = std::ceil(0x7fffffff / static_cast<double>(rate));
  } else {
    u32 rate = rates[roundToZero((attackRate ^ 0x7f) - 0x10) + 32];
    samples = 0x60000000 / rate;
    const u32 remainder = 0x60000000 % rate;
    rate = rates[roundToZero((attackRate ^ 0x7f) - 0x18) + 32];
    samples += std::ceil(std::max(0.0, 0x1fffffff - static_cast<double>(remainder)) / static_cast<double>(rate));
  }
  const double attackSeconds = samples / sampleRate;

  long envelopeLevel = 0x7fffffff;
  bool sustainLevelFound = false;
  u32 realSustainLevel = 0;
  int steps = 0;
  for (; envelopeLevel > 0; ++steps) {
    if (4 * (decayRate ^ 0x1f) < 0x18) {
      decayRate = 0;
    }
    switch ((envelopeLevel >> 28) & 0x7) {
      case 0:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 0) + 32];
        break;
      case 1:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 4) + 32];
        break;
      case 2:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 6) + 32];
        break;
      case 3:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 8) + 32];
        break;
      case 4:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 9) + 32];
        break;
      case 5:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 10) + 32];
        break;
      case 6:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 11) + 32];
        break;
      case 7:
        envelopeLevel -= rates[roundToZero((4 * (decayRate ^ 0x1f)) - 0x18 + 12) + 32];
        break;
      default:
        break;
    }
    if (!sustainLevelFound && ((envelopeLevel >> 27) & 0xf) <= sustainLevel) {
      realSustainLevel = envelopeLevel;
      sustainLevelFound = true;
    }
  }
  double decaySeconds = steps / sampleRate;

  envelopeLevel = 0x7fffffff;
  double sustainSeconds = -1.0;
  if (sustainDirection != 0 && sustainRate != 0x7f) {
    if (sustainMode == 0) {
      const u32 rate = rates[roundToZero((sustainRate ^ 0x7f) - 0x0f) + 32];
      samples = std::ceil(0x7fffffff / static_cast<double>(rate));
    } else {
      steps = 0;
      while (envelopeLevel > 0) {
        long envelopeLevelDiff = 0;
        long envelopeLevelTarget = 0;
        switch ((envelopeLevel >> 28) & 0x7) {
          case 0:
            envelopeLevelTarget = 0x00000000;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 0) + 32];
            break;
          case 1:
            envelopeLevelTarget = 0x0fffffff;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 4) + 32];
            break;
          case 2:
            envelopeLevelTarget = 0x1fffffff;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 6) + 32];
            break;
          case 3:
            envelopeLevelTarget = 0x2fffffff;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 8) + 32];
            break;
          case 4:
            envelopeLevelTarget = 0x3fffffff;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 9) + 32];
            break;
          case 5:
            envelopeLevelTarget = 0x4fffffff;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 10) + 32];
            break;
          case 6:
            envelopeLevelTarget = 0x5fffffff;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 11) + 32];
            break;
          case 7:
            envelopeLevelTarget = 0x6fffffff;
            envelopeLevelDiff = rates[roundToZero((sustainRate ^ 0x7f) - 0x1b + 12) + 32];
            break;
          default:
            break;
        }
        const long stepCount = (envelopeLevel - envelopeLevelTarget + (envelopeLevelDiff - 1)) / envelopeLevelDiff;
        envelopeLevel -= envelopeLevelDiff * stepCount;
        steps += static_cast<int>(stepCount);
      }
      samples = steps;
    }
    sustainSeconds = linearAmplitudeDecayToDbDecay(samples / sampleRate);
  }

  if (sustainLevel == 0) {
    realSustainLevel = 0x07ffffff;
  }
  double sustainAmplitude = realSustainLevel / static_cast<double>(0x7fffffff);
  if ((decaySeconds < 2.0 || (decayRate >= 0x0e && sustainLevel >= 0x0c)) && sustainRate < 0x7e &&
      sustainDirection == 1) {
    sustainAmplitude = 0.0;
    decaySeconds = sustainSeconds;
  }

  envelopeLevel = 0x7fffffff;
  if (releaseMode == 0) {
    const u32 rate = rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x0c) + 32];
    samples = rate != 0 ? std::ceil(static_cast<double>(envelopeLevel) / rate) : 0;
  } else {
    if ((releaseRate ^ 0x1f) * 4 < 0x18) {
      releaseRate = 0;
    }
    steps = 0;
    for (; envelopeLevel > 0; ++steps) {
      switch ((envelopeLevel >> 28) & 0x7) {
        case 0:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 0) + 32];
          break;
        case 1:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 4) + 32];
          break;
        case 2:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 6) + 32];
          break;
        case 3:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 8) + 32];
          break;
        case 4:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 9) + 32];
          break;
        case 5:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 10) + 32];
          break;
        case 6:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 11) + 32];
          break;
        case 7:
          envelopeLevel -= rates[roundToZero((4 * (releaseRate ^ 0x1f)) - 0x18 + 12) + 32];
          break;
        default:
          break;
      }
    }
    samples = steps;
  }
  const double releaseSeconds = linearAmplitudeDecayToDbDecay(samples / sampleRate);

  return Envelope{
      .attack = static_cast<u32>(std::clamp(std::llround(attackSeconds * 1'000'000.0), 0ll,
                                            static_cast<long long>(std::numeric_limits<u32>::max()))),
      .decay = decaySeconds < 0.0 ? kEnvelopeInfinite
                                  : static_cast<u32>(std::clamp(std::llround(decaySeconds * 1'000'000.0), 0ll,
                                                                static_cast<long long>(std::numeric_limits<u32>::max()))),
      .sustain = static_cast<u32>(std::round(sustainAmplitude * 1000.0)),
      .release = static_cast<u32>(std::clamp(std::llround(releaseSeconds * 1'000'000.0), 0ll,
                                             static_cast<long long>(std::numeric_limits<u32>::max()))),
      .attackSeconds = attackSeconds,
      .decaySeconds = decaySeconds,
      .releaseSeconds = releaseSeconds,
      .sustainAmplitude = sustainAmplitude,
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
  region.loop = art.loop;
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
    // Version 3 drum tables store the ADSR override once at the start of the
    // table. Individual drum entries still carry art, unity key, volume, and pan.
    const u8 attackRate = reader.u8At(offset + 2);
    const u8 sustainRate = reader.u8At(offset + 3);
    const u8 sustainMode = reader.u8At(offset + 4);
    const u8 releaseRate = reader.u8At(offset + 5);
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
          .pan = regionPanFromRaw(reader.u8At(regionOffset + 7) & 0x7f),
          .attenuationDb =
              attenuationDbFromLinear(reader.u8At(regionOffset + 6) == 0 ? 1.0 : reader.u8At(regionOffset + 6) / 128.0),
      };
      applyArtToRegion(region, findArt(artMap, artId), attackRate, sustainRate, sustainMode, releaseRate, true,
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
          .pan = regionPanFromRaw(reader.u8At(regionOffset + 4)),
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
        .loop = binding.art.loop,
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

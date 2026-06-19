/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSynth.h"

#include "value/scan/ScanResultBuilder.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr std::string_view kSwarSignature = "SWAR\xff\xfe\x00\x01";
constexpr double kEnvelopeIntervalSeconds = (2728.0 * 64.0) / 33513982.0;

constexpr std::array<s16, 128> kDecibelSquareTable = {
    -481, -480, -480, -480, -480, -480, -480, -480, -480, -460, -442, -425, -410, -396, -383, -371, -360, -349, -339,
    -330, -321, -313, -305, -297, -289, -282, -276, -269, -263, -257, -251, -245, -239, -234, -229, -224, -219, -214,
    -210, -205, -201, -196, -192, -188, -184, -180, -176, -173, -169, -165, -162, -158, -155, -152, -149, -145, -142,
    -139, -136, -133, -130, -127, -125, -122, -119, -116, -114, -111, -109, -106, -103, -101, -99,  -96,  -94,  -91,
    -89,  -87,  -85,  -82,  -80,  -78,  -76,  -74,  -72,  -70,  -68,  -66,  -64,  -62,  -60,  -58,  -56,  -54,  -52,
    -50,  -49,  -47,  -45,  -43,  -42,  -40,  -38,  -36,  -35,  -33,  -31,  -30,  -28,  -27,  -25,  -23,  -22,  -20,
    -19,  -17,  -16,  -14,  -13,  -11,  -10,  -8,   -7,   -6,   -4,   -3,   -1,   0};

constexpr std::array<u8, 19> kAttackTimeTable = {0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26, 0x33, 0x3F, 0x49, 0x54,
                                                 0x5C, 0x64, 0x6D, 0x74, 0x7B, 0x7F, 0x84, 0x89, 0x8F};

[[nodiscard]] bool matches(ByteReader reader, u64 offset, std::string_view signature) {
  if (!reader.has(offset, signature.size())) {
    return false;
  }
  for (size_t i = 0; i < signature.size(); ++i) {
    if (reader.u8At(offset + i) != static_cast<u8>(signature[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] u32 envelopeMicros(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) {
    return kEnvelopeInfinite;
  }
  const double micros = seconds * 1'000'000.0;
  if (micros >= static_cast<double>(std::numeric_limits<u32>::max())) {
    return std::numeric_limits<u32>::max();
  }
  return static_cast<u32>(std::lround(std::max(0.0, micros)));
}

[[nodiscard]] u32 envelopePermille(double level) {
  return static_cast<u32>(std::lround(std::clamp(level, 0.0, 1.0) * 1000.0));
}

[[nodiscard]] std::optional<u32> checkedAdd(u32 lhs, u32 rhs) {
  if (lhs > std::numeric_limits<u32>::max() - rhs) {
    return std::nullopt;
  }
  return lhs + rhs;
}

[[nodiscard]] std::optional<u32> checkedMul(u32 lhs, u32 rhs) {
  if (lhs != 0 && rhs > std::numeric_limits<u32>::max() / lhs) {
    return std::nullopt;
  }
  return lhs * rhs;
}

[[nodiscard]] u16 fallingRate(u8 decayTime) {
  if (decayTime == 0x7f) {
    return 0xffff;
  }
  if (decayTime == 0x7e) {
    return 0x3c00;
  }
  if (decayTime < 0x32) {
    return static_cast<u16>((decayTime * 2 + 1) & 0xffff);
  }
  return static_cast<u16>((0x1e00 / (0x7e - decayTime)) & 0xffff);
}

[[nodiscard]] std::optional<Envelope> ndsEnvelope(ByteReader reader, u32 offset) {
  // NDS envelopes use driver rate tables rather than SF2/DLS units. Preserve both rounded
  // microseconds and precise seconds so exporters can choose the most accurate conversion.
  const u8 attackTime = reader.u8At(offset + 1);
  const u8 decayTime = reader.u8At(offset + 2);
  const u8 sustainLevel = reader.u8At(offset + 3);
  const u8 releaseTime = reader.u8At(offset + 4);
  if (attackTime > 0x7f || decayTime > 0x7f || sustainLevel > 0x7f || releaseTime > 0x7f) {
    return std::nullopt;
  }

  u8 realAttack = 0xff - attackTime;
  if (attackTime >= 0x6d) {
    realAttack = kAttackTimeTable[0x7f - attackTime];
  }

  int count = 0;
  constexpr long attackThreshold = 0x16980 / 10;
  for (long value = 0x16980; value > attackThreshold; value = (value * realAttack) >> 8) {
    ++count;
  }
  const double attackSeconds = count * kEnvelopeIntervalSeconds;

  double sustainAmplitude = 0.0;
  if (sustainLevel == 0x7f) {
    sustainAmplitude = 1.0;
  } else if (sustainLevel != 0) {
    sustainAmplitude = std::pow(10.0, (kDecibelSquareTable[sustainLevel] / 10.0) / 20.0);
  }

  const u16 realDecay = fallingRate(decayTime);
  const u16 realRelease = fallingRate(releaseTime);
  const double decaySeconds = decayTime == 0x7f ? 0.001 : ((0x16980 / realDecay) * kEnvelopeIntervalSeconds);
  const std::optional<double> releaseSeconds =
      releaseTime == 0x7f ? std::nullopt : std::optional<double>{(0x16980 / realRelease) * kEnvelopeIntervalSeconds};

  return Envelope{
      .attack = envelopeMicros(attackSeconds),
      .decay = envelopeMicros(decaySeconds),
      .sustain = envelopePermille(sustainAmplitude),
      .release = releaseSeconds ? envelopeMicros(*releaseSeconds) : kEnvelopeInfinite,
      .attackSeconds = attackSeconds,
      .decaySeconds = decaySeconds,
      .releaseSeconds = releaseSeconds,
      .sustainAmplitude = sustainAmplitude,
  };
}

[[nodiscard]] double ndsPan(u8 pan) {
  if (pan == 0) {
    return 0.0;
  }
  if (pan == 127) {
    return 1.0;
  }
  if (pan == 64) {
    return 0.5;
  }
  return static_cast<double>(pan) / 127.0;
}

[[nodiscard]] std::optional<Region> ndsRegion(ByteReader reader, u32 offset, u32 length, SampleRef sample,
                                              u8 keyLow = 0, u8 keyHigh = 127,
                                              std::optional<u8> forcedRootKey = std::nullopt) {
  const u32 articulationOffset = offset + length - 6;
  if (!reader.has(articulationOffset, 6)) {
    return std::nullopt;
  }
  const u8 rootKey = reader.u8At(articulationOffset);
  const u8 pan = reader.u8At(articulationOffset + 5);
  const auto envelope = ndsEnvelope(reader, articulationOffset);
  if (!envelope || rootKey > 0x7f || pan > 0x7f) {
    return std::nullopt;
  }

  Region region{
      .keyRange = KeyRange{.low = keyLow, .high = keyHigh},
      .velocityRange = VelocityRange{.low = 0, .high = 127},
      .sample = sample,
      .range = reader.range(offset, length),
      .rootKey = forcedRootKey ? forcedRootKey : std::optional<u8>{rootKey},
      .envelope = *envelope,
      .pan = ndsPan(pan),
  };
  return region;
}

[[nodiscard]] std::optional<ScanSampleCollectionRef> bankWaveCollection(
    const std::array<std::optional<ScanSampleCollectionRef>, 4>& collections, u16 index) {
  if (index >= collections.size()) {
    return std::nullopt;
  }
  return collections[index];
}

void addRegion(std::vector<Region>& regions, std::optional<Region> region) {
  if (region) {
    regions.push_back(std::move(*region));
  }
}

// Tests can call the parser without a ScanResultBuilder. In that case the same
// checked reads run, but diagnostics are discarded.
[[nodiscard]] ParseCursor makeParseCursor(const ScanInput& input, SourceRange bounds, ScanResultBuilder* diagnostics,
                                          std::vector<Diagnostic>& ignoredDiagnostics) {
  if (diagnostics != nullptr) {
    return diagnostics->cursor(bounds);
  }
  return ParseCursor(input.reader, bounds, ignoredDiagnostics);
}

}  // namespace

bool isNdsWaveArchive(ByteReader reader, u32 offset) {
  return matches(reader, offset, kSwarSignature);
}

SampleCollectionAsset parseNdsPsgSamples(const ScanInput& input, AssetId id) {
  // PSG wave/noise instruments do not reference SWAR sample data. Emit a synthetic sample
  // collection so they can still participate in the same Instrument/Region model.
  SampleCollectionAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kNdsFormatName),
              .name = "NDS PSG samples",
              .range = input.reader.range(0, 0),
          },
  };
  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root =
      items.add(std::nullopt, ItemKind::SampleCollection, "nds-psg", "NDS PSG samples", input.reader.range(0, 0));

  for (u32 i = 0; i <= 8; ++i) {
    asset.samples.samples.push_back(Sample{
        .name = fmt::format("PSG_duty_{}", i),
        .codec = AudioCodec::NdsPsg,
        .encodedData = input.reader.range(0, 0),
        .sampleRate = 32768,
        .channels = 1,
        .bitsPerSample = 16,
        .loop = Loop{.enabled = true, .start = 0, .length = 32768},
        .codecParameter = i,
    });
    static_cast<void>(
        items.add(root, ItemKind::Sample, "nds-psg-sample", fmt::format("PSG_duty_{}", i), input.reader.range(0, 0)));
  }

  return asset;
}

SampleCollectionAsset parseNdsWaveArchive(const ScanInput& input, AssetId id, NdsFileRange range,
                                          const std::string& name, ScanResultBuilder* diagnostics) {
  // SWAR samples are compact wave records. ADPCM entries point encodedData after their
  // four-byte predictor header; SampleDecoder knows to read that header.
  SampleCollectionAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kNdsFormatName),
              .name = name,
              .range = input.reader.range(range.offset, range.size),
          },
  };
  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root =
      items.add(std::nullopt, ItemKind::SampleCollection, "swar", name, input.reader.range(range.offset, range.size));
  SourceMapBuilder* sourceMap = diagnostics != nullptr ? &diagnostics->sourceMap() : nullptr;

  if (!isNdsWaveArchive(input.reader, range.offset)) {
    return asset;
  }

  if (sourceMap != nullptr && input.reader.has(range.offset, 0x3c)) {
    sourceMap->header("SWAR Header", input.reader.range(range.offset, 0x3c)).kind("swar-header");
  }

  std::vector<Diagnostic> ignoredDiagnostics;
  auto archive = makeParseCursor(input, input.reader.range(range.offset, range.size), diagnostics, ignoredDiagnostics);
  const auto sampleCount = archive.le32(0x38, "SWAR sample count");
  if (!sampleCount) {
    return asset;
  }
  if (sourceMap != nullptr) {
    const u64 sampleTableSize = static_cast<u64>(*sampleCount) * 4;
    if (const auto sampleTableRange = archive.range(0x3c, sampleTableSize, "SWAR sample offset table")) {
      sourceMap->table("SWAR Sample Offset Table", *sampleTableRange)
          .kind("swar-sample-offset-table")
          .field("sample_count", sampleCount);
    }
  }

  for (u32 i = 0; i < *sampleCount; ++i) {
    const auto tableByteOffset = checkedMul(i, 4);
    const auto entryOffset = tableByteOffset ? checkedAdd(0x3c, *tableByteOffset) : std::nullopt;
    if (!entryOffset) {
      break;
    }
    const auto sampleRelativeOffset = archive.le32(*entryOffset, "SWAR sample offset");
    if (!sampleRelativeOffset) {
      break;
    }
    const auto sampleOffset = checkedAdd(range.offset, *sampleRelativeOffset);
    const auto sampleHeaderRange = archive.range(*sampleRelativeOffset, 0x0c, "SWAR sample header");
    if (!sampleOffset || !sampleHeaderRange) {
      continue;
    }
    if (sourceMap != nullptr) {
      sourceMap
          ->pointer("SWAR Sample Offset", sampleRelativeOffset.range, SourceTarget{*sampleHeaderRange})
          .kind("swar-sample-offset")
          .derived("sample_index", i);
    }
    auto sampleHeader = makeParseCursor(input, *sampleHeaderRange, diagnostics, ignoredDiagnostics);

    const auto waveType = sampleHeader.u8(0, "wave type");
    if (!waveType || *waveType > 2) {
      continue;
    }
    const auto loopFlag = sampleHeader.u8(1, "loop flag");
    const auto rawSampleRate = sampleHeader.le16(2, "sample rate");
    const auto timerValue = sampleHeader.le16(4, "timer value");
    const auto loopOffsetUnits = sampleHeader.le16(6, "loop offset");
    const auto nonLoopLengthUnits = sampleHeader.le16(8, "non-loop length");
    if (!loopFlag || !rawSampleRate || !timerValue || !loopOffsetUnits || !nonLoopLengthUnits) {
      continue;
    }

    const bool loops = *loopFlag != 0;
    u32 sampleRate = *rawSampleRate;
    if (*timerValue > 0) {
      sampleRate = 16756991 / *timerValue;
    }

    const u32 loopOffsetBytes = static_cast<u32>(*loopOffsetUnits) * 4;
    const u32 nonLoopLengthBytes = static_cast<u32>(*nonLoopLengthUnits) * 4;
    const auto totalDataBytes = checkedAdd(loopOffsetBytes, nonLoopLengthBytes);
    if (!totalDataBytes) {
      continue;
    }

    std::optional<u32> dataStartRelative = checkedAdd(*sampleRelativeOffset, 0x0c);
    u32 dataLength = *totalDataBytes;
    u32 loopStart = loopOffsetBytes;
    u32 loopLength = nonLoopLengthBytes;
    AudioCodec codec = AudioCodec::PcmS16;
    u16 bitsPerSample = 16;

    if (*waveType == 0) {
      codec = AudioCodec::PcmS8;
      bitsPerSample = 8;
    } else if (*waveType == 2) {
      codec = AudioCodec::NdsImaAdpcm;
      if (!archive.range(*sampleRelativeOffset, 0x10, "SWAR ADPCM sample header")) {
        continue;
      }
      dataStartRelative = checkedAdd(*sampleRelativeOffset, 0x10);
      if (*totalDataBytes < 4) {
        continue;
      }
      dataLength = *totalDataBytes - 4;

      const auto doubledDataLength = checkedMul(dataLength, 2);
      const auto decodedSampleCount = doubledDataLength ? checkedAdd(*doubledDataLength, 1) : std::nullopt;
      if (!decodedSampleCount) {
        continue;
      }

      // Legacy exports preserve ADPCM loop-offset metadata even when the SWAV
      // loop flag is clear. Keep that metadata for SF2/DLS headers while
      // Loop::enabled remains controlled by the loop flag.
      if (loopOffsetBytes >= 4) {
        loopStart = ((loopOffsetBytes - 4) * 2) + 1;
        if (loopStart > *decodedSampleCount) {
          continue;
        }
        loopLength = *decodedSampleCount - loopStart;
      } else if (loops) {
        continue;
      } else {
        loopStart = 0;
        loopLength = *decodedSampleCount;
      }
    }

    if (!dataStartRelative) {
      continue;
    }
    const auto dataRange = archive.range(*dataStartRelative, dataLength, "SWAR sample data");
    if (!dataRange || dataRange->endOffset() < *sampleOffset) {
      continue;
    }

    const u32 bytesPerFrame = *waveType == 1 ? 2 : 1;
    const Loop loop =
        *waveType == 2
            ? Loop{.enabled = loops, .start = loopStart, .length = loopLength}
            : Loop{.enabled = loops, .start = loopStart / bytesPerFrame, .length = loopLength / bytesPerFrame};
    const auto sampleIndex = static_cast<u32>(asset.samples.samples.size());
    const std::string sampleName = fmt::format("Sample {}", asset.samples.samples.size());
    if (sourceMap != nullptr) {
      sourceMap->header(sampleName + " Header", *sampleHeaderRange)
          .role(SourceRole::Sample)
          .kind("swar-sample-header")
          .owner(ObjectRefs::sample(id, sampleIndex))
          .field("wave_type", waveType, SourceValueDisplay::Hex)
          .field("loop_flag", loopFlag.range, loops, SourceValueDisplay::Boolean)
          .field("sample_rate", rawSampleRate.range, sampleRate);
    }
    asset.samples.samples.push_back(Sample{
        .name = sampleName,
        .codec = codec,
        .encodedData = *dataRange,
        .sampleRate = sampleRate,
        .channels = 1,
        .bitsPerSample = bitsPerSample,
        .loop = loop,
    });
    static_cast<void>(items.add(root, ItemKind::Sample, "swar-sample", sampleName,
                                input.reader.range(*sampleOffset, dataRange->endOffset() - *sampleOffset)));
  }

  return asset;
}

InstrumentSetAsset parseNdsInstrumentSet(const ScanInput& input, AssetId id, NdsFileRange range,
                                         const std::string& name, ScanResultBuilder& builder,
                                         ScanSampleCollectionRef psgCollection,
                                         const std::array<std::optional<ScanSampleCollectionRef>, 4>& waveCollections) {
  // SBNK instruments fan out by type: single region, PSG pulse/noise, drumset, or
  // key-split multi-region. Each case fills the same Instrument fields.
  InstrumentSetAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kNdsFormatName),
              .name = name,
              .range = input.reader.range(range.offset, range.size),
          },
  };
  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root =
      items.add(std::nullopt, ItemKind::InstrumentSet, "sbnk", name, input.reader.range(range.offset, range.size));

  if (!input.reader.has(range.offset + 0x3c, 4)) {
    return asset;
  }

  const u32 instrumentCount = input.reader.le32(range.offset + 0x38);
  const u64 instrumentTableSize = 4 + static_cast<u64>(instrumentCount) * 4;
  builder.sourceMap()
      .table("SBNK Instrument Pointer Table", input.reader.range(range.offset + 0x38, instrumentTableSize))
      .kind("sbnk-instrument-pointer-table")
      .field("instrument_count", input.reader.range(range.offset + 0x38, 4), instrumentCount);
  for (u32 i = 0; i < instrumentCount; ++i) {
    const u32 pointerOffset = range.offset + 0x3c + i * 4;
    if (!input.reader.has(pointerOffset, 4)) {
      break;
    }
    const u32 pointer = input.reader.le32(pointerOffset);
    if (pointer == 0) {
      continue;
    }

    const u8 instrumentType = pointer & 0xff;
    const u32 instrumentOffset = range.offset + (pointer >> 8);
    builder.sourceMap()
        .pointer("SBNK Instrument Pointer", input.reader.range(pointerOffset, 4),
                 SourceTarget{input.reader.range(instrumentOffset, 1)})
        .kind("sbnk-instrument-pointer")
        .derived("program", i)
        .field("type", input.reader.range(pointerOffset, 1), instrumentType, SourceValueDisplay::Hex);
    Instrument instrument{
        .bank = 0,
        .program = i,
        .reverb = 0.0,
        .name = "Instrument",
        .range = input.reader.range(instrumentOffset, 0),
    };

    switch (instrumentType) {
      case 0x01: {
        if (!input.reader.has(instrumentOffset, 10)) {
          break;
        }
        instrument.name = "Single-Region Instrument";
        instrument.range = input.reader.range(instrumentOffset, 10);
        const u16 sampleIndex = input.reader.le16(instrumentOffset);
        const u16 collectionIndex = input.reader.le16(instrumentOffset + 2);
        addRegion(instrument.regions,
                  ndsRegion(input.reader, instrumentOffset, 10,
                            builder.sampleRef(bankWaveCollection(waveCollections, collectionIndex), sampleIndex)));
        break;
      }
      case 0x02: {
        if (!input.reader.has(instrumentOffset, 10)) {
          break;
        }
        const u8 dutyCycle = input.reader.u8At(instrumentOffset) & 0x07;
        constexpr std::array<std::string_view, 8> dutyNames = {"12.5%", "25%", "37.5%", "50%",
                                                               "62.5%", "75%", "87.5%", "0%"};
        instrument.name = "PSG Wave (" + std::string(dutyNames[dutyCycle]) + ")";
        instrument.range = input.reader.range(instrumentOffset, 10);
        addRegion(instrument.regions, ndsRegion(input.reader, instrumentOffset, 10,
                                                builder.sampleRef(psgCollection, dutyCycle), 0, 127, 69));
        break;
      }
      case 0x03: {
        if (!input.reader.has(instrumentOffset, 10)) {
          break;
        }
        instrument.name = "PSG Noise";
        instrument.range = input.reader.range(instrumentOffset, 10);
        addRegion(instrument.regions,
                  ndsRegion(input.reader, instrumentOffset, 10, builder.sampleRef(psgCollection, 8), 0, 127, 45));
        break;
      }
      case 0x10: {
        // Drumsets encode one region per key in a contiguous key range.
        if (!input.reader.has(instrumentOffset, 2)) {
          break;
        }
        const u8 lowKey = input.reader.u8At(instrumentOffset);
        const u8 highKey = input.reader.u8At(instrumentOffset + 1);
        const u32 regionCount = highKey >= lowKey ? (highKey - lowKey) + 1 : 0;
        instrument.name = "Drumset";
        instrument.range = input.reader.range(instrumentOffset, 2 + regionCount * 12);
        for (u32 r = 0; r < regionCount; ++r) {
          const u32 regionOffset = instrumentOffset + 2 + r * 12;
          if (!input.reader.has(regionOffset, 12)) {
            break;
          }
          const u16 sampleIndex = input.reader.le16(regionOffset + 2);
          const u16 collectionIndex = input.reader.le16(regionOffset + 4);
          const auto key = static_cast<u8>(lowKey + r);
          addRegion(instrument.regions,
                    ndsRegion(input.reader, regionOffset, 12,
                              builder.sampleRef(bankWaveCollection(waveCollections, collectionIndex), sampleIndex), key,
                              key));
        }
        break;
      }
      case 0x11: {
        // Multi-region instruments store high-key boundaries followed by region records.
        if (!input.reader.has(instrumentOffset, 8)) {
          break;
        }
        std::array<u8, 8> keyRanges{};
        u32 regionCount = 0;
        for (u32 r = 0; r < keyRanges.size(); ++r) {
          keyRanges[r] = input.reader.u8At(instrumentOffset + r);
          if (keyRanges[r] == 0) {
            break;
          }
          ++regionCount;
        }
        instrument.name = "Multi-Region Instrument";
        instrument.range = input.reader.range(instrumentOffset, 8 + regionCount * 12);
        for (u32 r = 0; r < regionCount; ++r) {
          const u32 regionOffset = instrumentOffset + 8 + r * 12;
          if (!input.reader.has(regionOffset, 12)) {
            break;
          }
          const u16 sampleIndex = input.reader.le16(regionOffset + 2);
          const u16 collectionIndex = input.reader.le16(regionOffset + 4);
          const u8 keyLow = r == 0 ? 0 : static_cast<u8>(keyRanges[r - 1] + 1);
          addRegion(instrument.regions,
                    ndsRegion(input.reader, regionOffset, 12,
                              builder.sampleRef(bankWaveCollection(waveCollections, collectionIndex), sampleIndex),
                              keyLow, keyRanges[r]));
        }
        break;
      }
      default:
        break;
    }

    if (!instrument.regions.empty()) {
      auto annotation = builder.sourceMap()
                            .row(instrument.name, instrument.range)
                            .role(SourceRole::Instrument)
                            .kind("sbnk-instrument")
                            .owner(ObjectRefs::instrument(id, i))
                            .derived("program", i)
                            .derived("region_count", instrument.regions.size());
      for (const auto& region : instrument.regions) {
        if (region.sample.collection) {
          annotation.link(SourceLinkRole::UsesSample,
                          SourceTarget{ObjectRefs::sample(*region.sample.collection, region.sample.index)});
        }
      }
      const auto instrumentItem =
          items.add(root, ItemKind::Instrument, "instrument", instrument.name, instrument.range);
      for (const auto& region : instrument.regions) {
        static_cast<void>(items.add(instrumentItem, ItemKind::Region, "region", "Region", region.range));
      }
      asset.instruments.push_back(std::move(instrument));
    }
  }

  return asset;
}

}  // namespace vgmtrans::formats::nds

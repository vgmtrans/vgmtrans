/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/Nds.h"

#include "value/base/RecordReader.h"
#include "value/scan/BytePattern.h"
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
#include <vector>

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
constexpr std::array<std::string_view, 8> kDutyNames = {"12.5%", "25%", "37.5%", "50%", "62.5%", "75%", "87.5%", "0%"};
constexpr std::array<AudioCodec, 3> kWaveCodecs = {AudioCodec::PcmS8, AudioCodec::PcmS16, AudioCodec::NdsImaAdpcm};

enum class WaveType : u8 {
  Pcm8,
  Pcm16,
  ImaAdpcm,
};

enum class InstrumentType : u8 {
  Sample = 0x01,
  PsgWave = 0x02,
  PsgNoise = 0x03,
  Drumset = 0x10,
  KeySplit = 0x11,
};

// Converts an NDS decay or release byte into the rate used by the sound hardware.
[[nodiscard]] u16 fallingRate(u8 decayTime) {
  if (decayTime == 0x7f) {
    return 0xffff;
  }
  if (decayTime == 0x7e) {
    return 0x3c00;
  }
  if (decayTime < 0x32) {
    return static_cast<u16>(decayTime * 2 + 1);
  }
  return static_cast<u16>(0x1e00 / (0x7e - decayTime));
}

// Converts the four NDS envelope bytes into common attack, decay, sustain, and
// release values.
[[nodiscard]] std::optional<Envelope> ndsEnvelope(u8 attackTime, u8 decayTime, u8 sustainLevel, u8 releaseTime) {
  // NDS envelopes use driver rate tables rather than SF2/DLS units. Convert
  // them once to the shared physical representation.
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
  const double releaseSeconds = releaseTime == 0x7f ? std::numeric_limits<double>::infinity()
                                                    : (0x16980 / realRelease) * kEnvelopeIntervalSeconds;

  return Envelope{
      .attackSeconds = attackSeconds,
      .decaySeconds = decaySeconds,
      .releaseSeconds = releaseSeconds,
      .sustainAmplitude = sustainAmplitude,
  };
}

struct ParsedNdsRegion {
  Region region;
  SourceRecord source;
};

// All SBNK region kinds end with the same ten-byte body. Drum and key-split
// entries add a two-byte type prefix, which is retained in the source record.
[[nodiscard]] std::optional<ParsedNdsRegion> parseNdsRegion(
    ScanResultBuilder& builder, SourceRange range, InstrumentType type, const ScanSampleCollectionDraft& psgCollection,
    const std::array<std::optional<ScanSampleCollectionDraft>, 4>& waveCollections, KeyRange keys = {}) {
  if ((range.size != 10 && range.size != 12) || range.endOffset() > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }

  RecordReader record(builder.reader(), static_cast<u32>(range.offset), static_cast<u32>(range.endOffset()),
                      &builder.diagnostics());
  const u32 bodyOffset = static_cast<u32>(range.size - 10);
  if (bodyOffset == 2) {
    const auto regionType = record.u16leAt(0, "region_type", SourceValueDisplay::Hex);
    if (!regionType) {
      return std::nullopt;
    }
  }
  const auto sourceIndex = record.u16leAt(bodyOffset, "sample_index");
  const auto collectionIndex = record.u16leAt(bodyOffset + 2, "wave_archive");
  const auto rootKey = record.u8At(bodyOffset + 4, "root_key", SourceValueDisplay::MidiNote);
  const auto attack = record.u8At(bodyOffset + 5, "attack");
  const auto decay = record.u8At(bodyOffset + 6, "decay");
  const auto sustain = record.u8At(bodyOffset + 7, "sustain");
  const auto release = record.u8At(bodyOffset + 8, "release");
  const auto pan = record.u8At(bodyOffset + 9, "pan");
  if (!record.ok()) {
    return std::nullopt;
  }

  std::optional<SampleRef> sample;
  u8 effectiveRootKey = *rootKey;
  if (type == InstrumentType::Sample) {
    if (*collectionIndex < waveCollections.size() && waveCollections[*collectionIndex]) {
      sample = waveCollections[*collectionIndex]->find(*sourceIndex);
    }
    if (!sample) {
      builder.warning(fmt::format("Sample {} in wave archive slot {} was not found", *sourceIndex, *collectionIndex),
                      range);
    }
  } else {
    const bool pulse = type == InstrumentType::PsgWave;
    const u32 psgIndex = pulse ? (*sourceIndex & 0x07) : 8;
    const std::string description = pulse ? fmt::format("PSG duty sample {}", psgIndex) : "PSG noise sample";
    sample = psgCollection.find(psgIndex);
    if (!sample) {
      builder.warning(description + " was not found", range);
    }
    effectiveRootKey = pulse ? 69 : 45;
  }
  if (!sample) {
    return std::nullopt;
  }

  const auto envelope = ndsEnvelope(*attack, *decay, *sustain, *release);
  if (!envelope || *rootKey > 0x7f || *pan > 0x7f) {
    return std::nullopt;
  }

  return ParsedNdsRegion{
      .region =
          Region{
              .keyRange = keys,
              .sample = *sample,
              .range = range,
              .unityKey = static_cast<double>(effectiveRootKey),
              .envelope = *envelope,
              .pan = panPositionFrom7Bit(*pan),
          },
      .source = std::move(record).finish(),
  };
}

// Reads one SWAV entry and adds it under the source index used by SBNK records.
void addNdsWave(ScanResultBuilder& builder, RecordReader& archive, SampleCollectionBuilder& samples, u32 relativeOffset,
                SourceRange headerRange, u32 sampleIndex, SourceAnnotationId parent) {
  if (headerRange.endOffset() > std::numeric_limits<u32>::max()) {
    return;
  }

  RecordReader header(builder.reader(), static_cast<u32>(headerRange.offset), static_cast<u32>(headerRange.endOffset()),
                      &builder.diagnostics());
  const auto waveType = header.u8("wave_type", SourceValueDisplay::Hex);
  const auto loopFlag = header.u8("loop_flag", SourceValueDisplay::Boolean);
  const auto rawSampleRate = header.u16le("sample_rate");
  const auto timer = header.u16le("timer");
  const auto loopOffsetUnits = header.u16le("loop_offset");
  const auto nonLoopLengthUnits = header.u16le("non_loop_length");
  if (!header.ok() || !waveType || *waveType >= kWaveCodecs.size()) {
    return;
  }

  const auto type = static_cast<WaveType>(*waveType);
  const bool loops = *loopFlag != 0;
  const bool adpcm = type == WaveType::ImaAdpcm;
  const u32 sampleRate = *timer != 0 ? 16756991 / *timer : *rawSampleRate;
  const u32 loopOffsetBytes = static_cast<u32>(*loopOffsetUnits) * 4;
  const u32 nonLoopLengthBytes = static_cast<u32>(*nonLoopLengthUnits) * 4;
  const u32 totalDataBytes = loopOffsetBytes + nonLoopLengthBytes;
  if (adpcm && totalDataBytes < 4) {
    return;
  }

  // ADPCM stores a four-byte predictor header between the common SWAV header and
  // encodedData. SampleDecoder reads that predictor immediately before encodedData.
  if (adpcm && !archive.rangeAt(relativeOffset, 0x10, "SWAR ADPCM sample header")) {
    return;
  }
  const u32 dataLength = totalDataBytes - (adpcm ? 4 : 0);
  const u64 dataOffset = static_cast<u64>(relativeOffset) + (adpcm ? 0x10 : 0x0c);
  const auto dataRange = archive.rangeAt(dataOffset, dataLength, "SWAR sample data");
  if (!dataRange) {
    return;
  }

  u32 loopStart = loopOffsetBytes;
  u32 loopLength = nonLoopLengthBytes;
  if (adpcm) {
    const u32 decodedSampleCount = dataLength * 2 + 1;

    // Preserve the legacy ADPCM loop metadata even when looping is disabled;
    // SF2/DLS headers consume it independently of Loop::enabled.
    if (loopOffsetBytes >= 4) {
      loopStart = (loopOffsetBytes - 4) * 2 + 1;
      if (loopStart > decodedSampleCount) {
        return;
      }
      loopLength = decodedSampleCount - loopStart;
    } else if (loops) {
      return;
    } else {
      loopStart = 0;
      loopLength = decodedSampleCount;
    }
  } else if (type == WaveType::Pcm16) {
    loopStart /= 2;
    loopLength /= 2;
  }

  const std::string sampleName = fmt::format("Sample {}", sampleIndex);
  const SourceRecord source = std::move(header).finish();
  samples
      .add(sampleIndex,
           Sample{
               .name = sampleName,
               .codec = kWaveCodecs[*waveType],
               .encodedData = *dataRange,
               .sampleRate = sampleRate,
               .bitsPerSample = static_cast<u16>(type == WaveType::Pcm8 ? 8 : 16),
               .loop = Loop{.enabled = loops, .start = loopStart, .length = loopLength},
           })
      .source(sampleName + " Header", source, "swar-sample-header")
      .parent(parent);
}

}  // namespace

// Creates the built-in pulse and noise sounds used by NDS instruments that do
// not refer to a wave archive.
ScanSampleCollectionDraft addNdsPsgSamples(ScanResultBuilder& builder) {
  auto samples = builder.sampleCollection("NDS PSG samples");
  for (u32 i = 0; i <= 8; ++i) {
    samples.add(i, Sample{
                       .name = fmt::format("PSG_duty_{}", i),
                       .codec = AudioCodec::NdsPsg,
                       .encodedData = builder.reader().range(0, 0),
                       .sampleRate = 32768,
                       .loop = Loop{.enabled = true, .start = 0, .length = 32768},
                       .codecParameter = i,
                   });
  }

  return samples;
}

// Reads every valid sample from one NDS wave archive and adds the resulting
// collection to the scan result.
std::optional<ScanSampleCollectionDraft> addNdsWaveArchive(ScanResultBuilder& builder, SourceRange range,
                                                           std::string_view name) {
  const ByteReader reader = builder.reader();
  if (!matchesBytes(reader, range.offset, kSwarSignature)) {
    return std::nullopt;
  }

  auto samples = builder.sampleCollection(std::string(name));
  samples.include(range);

  if (range.endOffset() > std::numeric_limits<u32>::max()) {
    samples.warning("NDS wave archive range is too large to parse", range);
    return samples;
  }
  RecordReader archive(reader, static_cast<u32>(range.offset), static_cast<u32>(range.endOffset()),
                       &builder.diagnostics(), false);
  if (const auto header = archive.rangeAt(0, 0x3c, "SWAR header")) {
    samples.source(SourceRole::Header, "SWAR Header", *header, "swar-header");
  }

  const auto sampleCount = archive.u32leAt(0x38, "SWAR sample count");
  if (!sampleCount) {
    return samples;
  }
  const auto sampleTableRange = archive.rangeAt(0x3c, static_cast<u64>(*sampleCount) * 4, "SWAR sample offset table");
  if (!sampleTableRange) {
    return samples;
  }
  const SourceAnnotationId sampleTable =
      samples.source(SourceRole::Table, "SWAR Sample Offset Table", *sampleTableRange, "swar-sample-offset-table")
          .field("sample_count", sampleCount)
          .id();

  for (u32 i = 0; i < *sampleCount; ++i) {
    const u64 entryOffset = 0x3c + static_cast<u64>(i) * 4;
    const auto sampleRelativeOffset = archive.u32leAt(entryOffset, "SWAR sample offset");
    if (!sampleRelativeOffset) {
      break;
    }
    const auto sampleHeaderRange = archive.rangeAt(*sampleRelativeOffset, 0x0c, "SWAR sample header");
    if (!sampleHeaderRange) {
      continue;
    }
    const SourceAnnotationId samplePointer =
        builder.sourceMap()
            .pointer("SWAR Sample Offset", sampleRelativeOffset.range, SourceTarget{*sampleHeaderRange})
            .kind("swar-sample-offset")
            .derived("sample_index", i)
            .parent(sampleTable)
            .id();
    addNdsWave(builder, archive, samples.builder(), *sampleRelativeOffset, *sampleHeaderRange, i, samplePointer);
  }

  return samples;
}

// Reads the instruments in one NDS bank, including single-sample, pulse, noise,
// drum, and key-split instruments.
std::optional<ScanInstrumentSetDraft> addNdsInstrumentSet(
    ScanResultBuilder& builder, SourceRange range, std::string_view name,
    const ScanSampleCollectionDraft& psgCollection,
    const std::array<std::optional<ScanSampleCollectionDraft>, 4>& waveCollections) {
  const ByteReader reader = builder.reader();
  if (range.endOffset() > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  RecordReader bank(reader, static_cast<u32>(range.offset), static_cast<u32>(range.endOffset()), &builder.diagnostics(),
                    false);
  const auto instrumentCount = bank.u32leAt(0x38, "SBNK instrument count");
  if (!instrumentCount) {
    return std::nullopt;
  }
  const auto pointerTableRange =
      bank.rangeAt(0x38, 4 + static_cast<u64>(*instrumentCount) * 4, "SBNK instrument pointer table");
  if (!pointerTableRange) {
    return std::nullopt;
  }

  auto instruments = builder.instrumentSet(std::string(name));
  instruments.include(range);
  auto pointerTable = instruments
                          .source(SourceRole::Table, "SBNK Instrument Pointer Table", *pointerTableRange,
                                  "sbnk-instrument-pointer-table")
                          .field("instrument_count", instrumentCount);

  for (u32 i = 0; i < *instrumentCount; ++i) {
    const auto pointer = bank.u32leAt(0x3c + static_cast<u64>(i) * 4, "SBNK instrument pointer");
    if (!pointer) {
      break;
    }
    if (*pointer == 0) {
      continue;
    }

    const u8 rawType = *pointer & 0xff;
    const auto type = static_cast<InstrumentType>(rawType);
    const u32 instrumentRelativeOffset = *pointer >> 8;
    const auto instrumentStart = bank.rangeAt(instrumentRelativeOffset, 1, "SBNK instrument");
    if (!instrumentStart) {
      continue;
    }
    const u64 instrumentOffset = instrumentStart->offset;
    auto pointerAnnotation =
        builder.sourceMap()
            .pointer("SBNK Instrument Pointer", pointer.range, SourceTarget{*instrumentStart})
            .kind("sbnk-instrument-pointer")
            .derived("program", i)
            .field("type", reader.range(pointer.range.offset, 1), rawType, SourceValueDisplay::Hex);
    pointerAnnotation.parent(pointerTable.id());
    Instrument instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = i},
        .reverb = 0.0,
        .name = "Instrument",
        .range = reader.range(instrumentOffset, 0),
    };
    std::vector<ParsedNdsRegion> parsedRegions;
    const auto addRegion = [&](std::optional<ParsedNdsRegion> parsed) {
      if (parsed) {
        parsedRegions.push_back(std::move(*parsed));
      }
    };

    switch (type) {
      case InstrumentType::Sample:
      case InstrumentType::PsgWave:
      case InstrumentType::PsgNoise: {
        const auto recordRange = bank.rangeAt(instrumentRelativeOffset, 10, "SBNK single-region instrument");
        if (!recordRange) {
          break;
        }
        instrument.range = *recordRange;
        auto region = parseNdsRegion(builder, *recordRange, type, psgCollection, waveCollections);
        if (type == InstrumentType::Sample) {
          instrument.name = "Single-Region Instrument";
        } else if (type == InstrumentType::PsgWave) {
          const u32 dutyCycle = region ? region->region.sample.index : 0;
          instrument.name = "PSG Wave (" + std::string(kDutyNames[dutyCycle]) + ")";
        } else {
          instrument.name = "PSG Noise";
        }
        addRegion(std::move(region));
        break;
      }
      case InstrumentType::Drumset: {
        // Drumsets encode one region per key in a contiguous key range.
        if (!bank.rangeAt(instrumentRelativeOffset, 2, "SBNK drumset header")) {
          break;
        }
        const u8 lowKey = reader.u8At(instrumentOffset);
        const u8 highKey = reader.u8At(instrumentOffset + 1);
        const u32 regionCount = highKey >= lowKey ? (highKey - lowKey) + 1 : 0;
        const auto instrumentRange =
            bank.rangeAt(instrumentRelativeOffset, 2 + regionCount * 12, "SBNK drumset regions");
        if (!instrumentRange) {
          break;
        }
        instrument.name = "Drumset";
        instrument.range = *instrumentRange;
        for (u32 r = 0; r < regionCount; ++r) {
          const SourceRange regionRange = reader.range(instrumentOffset + 2 + r * 12, 12);
          const auto key = static_cast<u8>(lowKey + r);
          addRegion(parseNdsRegion(builder, regionRange, InstrumentType::Sample, psgCollection, waveCollections,
                                   KeyRange{.low = key, .high = key}));
        }
        break;
      }
      case InstrumentType::KeySplit: {
        // Multi-region instruments store high-key boundaries followed by region records.
        if (!bank.rangeAt(instrumentRelativeOffset, 8, "SBNK key-split header")) {
          break;
        }
        std::array<u8, 8> keyRanges{};
        u32 regionCount = 0;
        for (u32 r = 0; r < keyRanges.size(); ++r) {
          keyRanges[r] = reader.u8At(instrumentOffset + r);
          if (keyRanges[r] == 0) {
            break;
          }
          ++regionCount;
        }
        const auto instrumentRange =
            bank.rangeAt(instrumentRelativeOffset, 8 + regionCount * 12, "SBNK key-split regions");
        if (!instrumentRange) {
          break;
        }
        instrument.name = "Multi-Region Instrument";
        instrument.range = *instrumentRange;
        for (u32 r = 0; r < regionCount; ++r) {
          const SourceRange regionRange = reader.range(instrumentOffset + 8 + r * 12, 12);
          const u8 keyLow = r == 0 ? 0 : static_cast<u8>(keyRanges[r - 1] + 1);
          const KeyRange keys{.low = keyLow, .high = keyRanges[r]};
          addRegion(parseNdsRegion(builder, regionRange, InstrumentType::Sample, psgCollection, waveCollections, keys));
        }
        break;
      }
      default:
        break;
    }

    if (parsedRegions.empty()) {
      continue;
    }

    const std::string instrumentName = instrument.name;
    const SourceRange instrumentRange = instrument.range;
    auto entry = instruments.add(i, std::move(instrument));
    entry.source(instrumentName, instrumentRange, "sbnk-instrument").parent(pointerAnnotation.id());
    for (auto& parsed : parsedRegions) {
      const SampleRef sample = parsed.region.sample;
      entry.region(sample, std::move(parsed.region)).source("Region", parsed.source, "sbnk-region");
    }
  }

  return instruments;
}

}  // namespace vgmtrans::formats::nds

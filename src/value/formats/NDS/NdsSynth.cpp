/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/Nds.h"

#include "value/base/RecordReader.h"
#include "value/scan/BytePattern.h"
#include "value/scan/ScanResultBuilder.h"

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

// Converts a time in seconds into the whole microseconds stored in an envelope.
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

// Converts a level from zero-to-one into thousandths for the shared envelope.
[[nodiscard]] u32 envelopePermille(double level) {
  return static_cast<u32>(std::lround(std::clamp(level, 0.0, 1.0) * 1000.0));
}

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
[[nodiscard]] std::optional<Envelope> ndsEnvelope(ByteReader reader, u64 offset) {
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

// Converts the NDS pan byte into the zero-to-one position stored in a region.
[[nodiscard]] double ndsPan(u8 pan) {
  return pan == 64 ? 0.5 : static_cast<double>(pan) / 127.0;
}

// Reads the tuning, envelope, and pan shared by every kind of NDS instrument region.
[[nodiscard]] std::optional<Region> parseNdsRegion(ByteReader reader, SourceRange range, SampleRef sample,
                                                   KeyRange keys = {},
                                                   std::optional<u8> rootKeyOverride = std::nullopt) {
  if (!range.valid() || range.source != reader.source() || range.size < 6 || !reader.has(range.offset, range.size)) {
    return std::nullopt;
  }
  const u64 articulationOffset = range.endOffset() - 6;
  const u8 rootKey = reader.u8At(articulationOffset);
  const u8 pan = reader.u8At(articulationOffset + 5);
  const auto envelope = ndsEnvelope(reader, articulationOffset);
  if (!envelope || rootKey > 0x7f || pan > 0x7f) {
    return std::nullopt;
  }

  return Region{
      .keyRange = keys,
      .sample = sample,
      .range = range,
      .rootKey = rootKeyOverride.value_or(rootKey),
      .envelope = *envelope,
      .pan = ndsPan(pan),
  };
}

// Reads a sample-backed region and connects its wave-archive and sample numbers
// to the matching sample asset.
[[nodiscard]] std::optional<Region> parseNdsSampleRegion(
    ScanResultBuilder& builder, ByteReader reader, SourceRange range,
    const std::array<std::optional<ScanSampleCollectionRef>, 4>& waveCollections, KeyRange keys = {}) {
  // Every sample-backed SBNK region ends with a four-byte sample reference and
  // the same six-byte articulation, regardless of its enclosing instrument type.
  if (range.size < 10) {
    return std::nullopt;
  }
  const u64 sampleOffset = range.endOffset() - 10;
  const u16 sampleIndex = reader.le16(sampleOffset);
  const u16 collectionIndex = reader.le16(sampleOffset + 2);
  const auto collection = collectionIndex < waveCollections.size() ? waveCollections[collectionIndex] : std::nullopt;
  const auto sample = builder.sampleByKeyOrWarning(
      collection, sampleIndex, fmt::format("Sample {} in wave archive slot {}", sampleIndex, collectionIndex), range);
  if (!sample) {
    return std::nullopt;
  }
  return parseNdsRegion(reader, range, *sample, keys);
}

// Reads one SWAV entry and adds it under the source index used by SBNK records.
void addNdsWave(ScanResultBuilder& builder, ParseCursor& archive, SampleCollectionBuilder& samples, u32 relativeOffset,
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
  if (adpcm && !archive.range(relativeOffset, 0x10, "SWAR ADPCM sample header")) {
    return;
  }
  const u32 dataLength = totalDataBytes - (adpcm ? 4 : 0);
  const u64 dataOffset = static_cast<u64>(relativeOffset) + (adpcm ? 0x10 : 0x0c);
  const auto dataRange = archive.range(dataOffset, dataLength, "SWAR sample data");
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
  header.derived("effective_sample_rate", sampleRate);
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
      .source(sampleName + " Header", headerRange, "swar-sample-header")
      .fields(header.takeFields())
      .parent(parent);
}

}  // namespace

// Creates the built-in pulse and noise sounds used by NDS instruments that do
// not refer to a wave archive.
ScanSampleCollectionRef addNdsPsgSamples(ScanResultBuilder& builder) {
  auto samples = builder.samples();
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

  return builder.sampleCollection("NDS PSG samples", std::move(samples));
}

// Reads every valid sample from one NDS wave archive and adds the resulting
// collection to the scan result.
std::optional<ScanSampleCollectionRef> addNdsWaveArchive(ScanResultBuilder& builder, SourceRange range,
                                                         std::string_view name) {
  const ByteReader reader = builder.reader();
  if (!matchesBytes(reader, range.offset, kSwarSignature)) {
    return std::nullopt;
  }

  auto samples = builder.samples();
  samples.include(range);
  const auto commit = [&]() { return builder.sampleCollection(std::string(name), std::move(samples)); };

  auto archive = builder.cursor(range);
  if (const auto header = archive.range(0, 0x3c, "SWAR header")) {
    samples.source(SourceRole::Header, "SWAR Header", *header, "swar-header");
  }

  const auto sampleCount = archive.le32(0x38, "SWAR sample count");
  if (!sampleCount) {
    return commit();
  }
  const auto sampleTableRange = archive.range(0x3c, static_cast<u64>(*sampleCount) * 4, "SWAR sample offset table");
  if (!sampleTableRange) {
    return commit();
  }
  const SourceAnnotationId sampleTable =
      samples.source(SourceRole::Table, "SWAR Sample Offset Table", *sampleTableRange, "swar-sample-offset-table")
          .field("sample_count", sampleCount)
          .id();

  for (u32 i = 0; i < *sampleCount; ++i) {
    const u64 entryOffset = 0x3c + static_cast<u64>(i) * 4;
    const auto sampleRelativeOffset = archive.le32(entryOffset, "SWAR sample offset");
    if (!sampleRelativeOffset) {
      break;
    }
    const auto sampleHeaderRange = archive.range(*sampleRelativeOffset, 0x0c, "SWAR sample header");
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
    addNdsWave(builder, archive, samples, *sampleRelativeOffset, *sampleHeaderRange, i, samplePointer);
  }

  return commit();
}

// Reads the instruments in one NDS bank, including single-sample, pulse, noise,
// drum, and key-split instruments.
std::optional<ScanInstrumentSetRef> addNdsInstrumentSet(
    ScanResultBuilder& builder, SourceRange range, std::string_view name, ScanSampleCollectionRef psgCollection,
    const std::array<std::optional<ScanSampleCollectionRef>, 4>& waveCollections) {
  const ByteReader reader = builder.reader();
  auto bank = builder.cursor(range);
  const auto instrumentCount = bank.le32(0x38, "SBNK instrument count");
  if (!instrumentCount) {
    return std::nullopt;
  }
  const auto pointerTableRange =
      bank.range(0x38, 4 + static_cast<u64>(*instrumentCount) * 4, "SBNK instrument pointer table");
  if (!pointerTableRange) {
    return std::nullopt;
  }

  auto instruments = builder.instruments();
  instruments.include(range);
  auto pointerTable = instruments
                          .source(SourceRole::Table, "SBNK Instrument Pointer Table", *pointerTableRange,
                                  "sbnk-instrument-pointer-table")
                          .field("instrument_count", instrumentCount);

  for (u32 i = 0; i < *instrumentCount; ++i) {
    const auto pointer = bank.le32(0x3c + static_cast<u64>(i) * 4, "SBNK instrument pointer");
    if (!pointer) {
      break;
    }
    if (*pointer == 0) {
      continue;
    }

    const u8 rawType = *pointer & 0xff;
    const auto type = static_cast<InstrumentType>(rawType);
    const u32 instrumentRelativeOffset = *pointer >> 8;
    const auto instrumentStart = bank.range(instrumentRelativeOffset, 1, "SBNK instrument");
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

    switch (type) {
      case InstrumentType::Sample:
      case InstrumentType::PsgWave:
      case InstrumentType::PsgNoise: {
        const auto recordRange = bank.range(instrumentRelativeOffset, 10, "SBNK single-region instrument");
        if (!recordRange) {
          break;
        }
        instrument.range = *recordRange;
        std::optional<Region> region;
        if (type == InstrumentType::Sample) {
          instrument.name = "Single-Region Instrument";
          region = parseNdsSampleRegion(builder, reader, *recordRange, waveCollections);
        } else if (type == InstrumentType::PsgWave) {
          const u8 dutyCycle = reader.u8At(instrumentOffset) & 0x07;
          instrument.name = "PSG Wave (" + std::string(kDutyNames[dutyCycle]) + ")";
          if (const auto sample = builder.sampleByKeyOrWarning(
                  psgCollection, dutyCycle, fmt::format("PSG duty sample {}", dutyCycle), *recordRange)) {
            region = parseNdsRegion(reader, *recordRange, *sample, {}, 69);
          }
        } else {
          instrument.name = "PSG Noise";
          if (const auto sample = builder.sampleByKeyOrWarning(psgCollection, 8, "PSG noise sample", *recordRange)) {
            region = parseNdsRegion(reader, *recordRange, *sample, {}, 45);
          }
        }
        if (region) {
          instrument.regions.push_back(std::move(*region));
        }
        break;
      }
      case InstrumentType::Drumset: {
        // Drumsets encode one region per key in a contiguous key range.
        if (!bank.range(instrumentRelativeOffset, 2, "SBNK drumset header")) {
          break;
        }
        const u8 lowKey = reader.u8At(instrumentOffset);
        const u8 highKey = reader.u8At(instrumentOffset + 1);
        const u32 regionCount = highKey >= lowKey ? (highKey - lowKey) + 1 : 0;
        const auto instrumentRange = bank.range(instrumentRelativeOffset, 2 + regionCount * 12, "SBNK drumset regions");
        if (!instrumentRange) {
          break;
        }
        instrument.name = "Drumset";
        instrument.range = *instrumentRange;
        for (u32 r = 0; r < regionCount; ++r) {
          const SourceRange regionRange = reader.range(instrumentOffset + 2 + r * 12, 12);
          const auto key = static_cast<u8>(lowKey + r);
          if (auto region = parseNdsSampleRegion(builder, reader, regionRange, waveCollections,
                                                 KeyRange{.low = key, .high = key})) {
            instrument.regions.push_back(std::move(*region));
          }
        }
        break;
      }
      case InstrumentType::KeySplit: {
        // Multi-region instruments store high-key boundaries followed by region records.
        if (!bank.range(instrumentRelativeOffset, 8, "SBNK key-split header")) {
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
            bank.range(instrumentRelativeOffset, 8 + regionCount * 12, "SBNK key-split regions");
        if (!instrumentRange) {
          break;
        }
        instrument.name = "Multi-Region Instrument";
        instrument.range = *instrumentRange;
        for (u32 r = 0; r < regionCount; ++r) {
          const SourceRange regionRange = reader.range(instrumentOffset + 8 + r * 12, 12);
          const u8 keyLow = r == 0 ? 0 : static_cast<u8>(keyRanges[r - 1] + 1);
          const KeyRange keys{.low = keyLow, .high = keyRanges[r]};
          if (auto region = parseNdsSampleRegion(builder, reader, regionRange, waveCollections, keys)) {
            instrument.regions.push_back(std::move(*region));
          }
        }
        break;
      }
      default:
        break;
    }

    if (instrument.regions.empty()) {
      continue;
    }

    auto entry = instruments.add(i, std::move(instrument));
    entry.source(entry.value().name, entry.value().range, "sbnk-instrument")
        .derived("program", i)
        .derived("region_count", entry.value().regions.size())
        .parent(pointerAnnotation.id());
  }

  return builder.instrumentSet(std::string(name), std::move(instruments));
}

}  // namespace vgmtrans::formats::nds

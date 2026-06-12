/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsModule.h"

#include "value/core/FormatRegistry.h"
#include "value/core/SynthMath.h"
#include "value/formats/NDS/NdsSequenceProgram.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

// NDS SDAT containers hold several related tables: SSEQ sequences, SBNK banks, and
// SWAR wave archives. The scanner resolves that graph into value assets and collections.

constexpr std::string_view kFormatName = "NDS";
constexpr std::string_view kSdatSignature = "SDAT\xff\xfe\x00\x01";
constexpr std::string_view kSseqSignature = "SSEQ\xff\xfe\x00\x01";
constexpr std::string_view kSwarSignature = "SWAR\xff\xfe\x00\x01";
constexpr u32 kMaxNameLength = 128;
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

struct FileRange {
  u32 offset = 0;
  u32 size = 0;
};

struct SequenceInfo {
  bool valid = false;
  u16 fileId = 0xffff;
  u16 bank = 0xffff;
};

struct BankInfo {
  bool valid = false;
  u16 fileId = 0xffff;
  std::array<u16, 4> waveArchives{0xffff, 0xffff, 0xffff, 0xffff};
};

struct WaveArchiveInfo {
  bool valid = false;
  u16 fileId = 0xffff;
};

struct SdatInfo {
  // Parsed SDAT table-of-contents. File IDs refer into FAT; sequence/bank/wave indexes
  // refer into INFO/SYMB tables.
  u32 baseOffset = 0;
  u32 length = 0;
  u32 symbOffset = 0;
  u32 infoOffset = 0;
  u32 fatOffset = 0;
  bool hasSymb = false;
  std::vector<std::string> sequenceNames;
  std::vector<std::string> bankNames;
  std::vector<std::string> waveArchiveNames;
  std::vector<SequenceInfo> sequences;
  std::vector<BankInfo> banks;
  std::vector<WaveArchiveInfo> waveArchives;
};

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

[[nodiscard]] std::optional<u32> nearbySseqHeader(ByteReader reader, FileRange range) {
  constexpr u32 kMaxPaddingBeforeSseq = 0x200;
  const u64 searchEnd =
      std::min<u64>(reader.size(), static_cast<u64>(range.offset) + range.size + kMaxPaddingBeforeSseq);
  for (u64 offset = range.offset + 1; offset + kSseqSignature.size() <= searchEnd; ++offset) {
    if (matches(reader, offset, kSseqSignature)) {
      return static_cast<u32>(offset);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool isZeroFilled(ByteReader reader, u32 begin, u32 end) {
  for (u32 offset = begin; offset < end && reader.has(offset, 1); ++offset) {
    if (reader.u8At(offset) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool shouldParseMalformedFallthrough(ByteReader reader, FileRange range) {
  const auto sseqOffset = nearbySseqHeader(reader, range);
  if (!sseqOffset) {
    return false;
  }

  const u32 trackStart = range.offset + 0x1c;
  const u32 paddingEnd = std::min(*sseqOffset, range.offset + range.size);
  // Some zero-filled pseudo-sequences overlap a later SSEQ. If the padding
  // would align the SSEQ signature as bogus note data, leave it empty.
  if (range.size <= 0x100 && *sseqOffset >= trackStart && isZeroFilled(reader, range.offset, paddingEnd) &&
      ((*sseqOffset - trackStart) % 3) == 2) {
    return false;
  }
  return true;
}

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

[[nodiscard]] std::string fallbackName(std::string_view prefix, u32 index) {
  return fmt::format("{}_{:04d}", prefix, index);
}

[[nodiscard]] std::string nullTerminatedString(ByteReader reader, u64 offset, u64 maxLength) {
  if (offset >= reader.size()) {
    return {};
  }
  const u64 limit = std::min<u64>(maxLength, reader.size() - offset);
  std::string result;
  result.reserve(static_cast<size_t>(limit));
  for (u64 i = 0; i < limit; ++i) {
    const u8 value = reader.u8At(offset + i);
    if (value == 0) {
      break;
    }
    result.push_back(static_cast<char>(value));
  }
  return result;
}

[[nodiscard]] u32 readCountFromInfoList(ByteReader reader, u32 infoOffset, u32 tablePointerField) {
  if (!reader.has(infoOffset + tablePointerField, 4)) {
    return 0;
  }
  const u32 listOffset = reader.le32(infoOffset + tablePointerField) + infoOffset;
  if (!reader.has(listOffset, 4)) {
    return 0;
  }
  return reader.le32(listOffset);
}

[[nodiscard]] std::vector<std::string> readNames(ByteReader reader, u32 symbOffset, u32 pointerListField, u32 count,
                                                 std::string_view fallbackPrefix, bool hasSymb) {
  std::vector<std::string> names;
  names.reserve(count);
  std::optional<u32> pointerList;
  if (hasSymb && reader.has(symbOffset + pointerListField, 4)) {
    pointerList = reader.le32(symbOffset + pointerListField) + symbOffset;
  }

  for (u32 i = 0; i < count; ++i) {
    std::string name;
    if (pointerList && reader.has(*pointerList + 4 + i * 4, 4)) {
      const u32 nameOffset = reader.le32(*pointerList + 4 + i * 4) + symbOffset;
      name = nullTerminatedString(reader, nameOffset, kMaxNameLength);
    }
    names.push_back(name.empty() ? fallbackName(fallbackPrefix, i) : std::move(name));
  }
  return names;
}

[[nodiscard]] std::optional<SdatInfo> parseSdatInfo(ByteReader reader, u32 baseOffset) {
  // SDAT stores most offsets relative to section starts. Normalize them to source offsets
  // immediately so later parsers can use SourceRange directly.
  if (!matches(reader, baseOffset, kSdatSignature) || !reader.has(baseOffset + 0x24, 4)) {
    return std::nullopt;
  }

  SdatInfo sdat{
      .baseOffset = baseOffset,
      .length = reader.le32(baseOffset + 8) + 8,
      .symbOffset = reader.le32(baseOffset + 0x10) + baseOffset,
      .infoOffset = reader.le32(baseOffset + 0x18) + baseOffset,
      .fatOffset = reader.le32(baseOffset + 0x20) + baseOffset,
  };
  sdat.hasSymb = sdat.symbOffset != baseOffset && reader.has(sdat.symbOffset, 0x18);
  if (!reader.has(sdat.infoOffset, 0x18) || !reader.has(sdat.fatOffset, 0x0c)) {
    return std::nullopt;
  }

  const u32 sequenceCount = readCountFromInfoList(reader, sdat.infoOffset, 0x08);
  const u32 bankCount = readCountFromInfoList(reader, sdat.infoOffset, 0x10);
  const u32 waveArchiveCount = readCountFromInfoList(reader, sdat.infoOffset, 0x14);

  sdat.sequenceNames = readNames(reader, sdat.symbOffset, 0x08, sequenceCount, "SSEQ", sdat.hasSymb);
  sdat.bankNames = readNames(reader, sdat.symbOffset, 0x10, bankCount, "SBNK", sdat.hasSymb);
  sdat.waveArchiveNames = readNames(reader, sdat.symbOffset, 0x14, waveArchiveCount, "SWAR", sdat.hasSymb);

  const u32 sequenceInfoList = reader.le32(sdat.infoOffset + 0x08) + sdat.infoOffset;
  sdat.sequences.reserve(sequenceCount);
  for (u32 i = 0; i < sequenceCount; ++i) {
    SequenceInfo info;
    if (reader.has(sequenceInfoList + 4 + i * 4, 4)) {
      const u32 relative = reader.le32(sequenceInfoList + 4 + i * 4);
      const u32 offset = sdat.infoOffset + relative;
      if (relative != 0 && reader.has(offset, 6)) {
        info.valid = true;
        info.fileId = reader.le16(offset);
        info.bank = reader.le16(offset + 4);
      } else if (reader.has(offset + 4, 2)) {
        info.bank = reader.le16(offset + 4);
      }
    }
    sdat.sequences.push_back(info);
  }

  const u32 bankInfoList = reader.le32(sdat.infoOffset + 0x10) + sdat.infoOffset;
  sdat.banks.reserve(bankCount);
  for (u32 i = 0; i < bankCount; ++i) {
    BankInfo info;
    if (reader.has(bankInfoList + 4 + i * 4, 4)) {
      const u32 relative = reader.le32(bankInfoList + 4 + i * 4);
      const u32 offset = sdat.infoOffset + relative;
      if (relative != 0 && reader.has(offset, 12)) {
        info.valid = true;
        info.fileId = reader.le16(offset);
        for (u32 j = 0; j < info.waveArchives.size(); ++j) {
          const u16 waveArchive = reader.le16(offset + 4 + j * 2);
          info.waveArchives[j] = waveArchive >= waveArchiveCount ? 0xffff : waveArchive;
        }
      }
    }
    sdat.banks.push_back(info);
  }

  const u32 waveArchiveInfoList = reader.le32(sdat.infoOffset + 0x14) + sdat.infoOffset;
  sdat.waveArchives.reserve(waveArchiveCount);
  for (u32 i = 0; i < waveArchiveCount; ++i) {
    WaveArchiveInfo info;
    if (reader.has(waveArchiveInfoList + 4 + i * 4, 4)) {
      const u32 relative = reader.le32(waveArchiveInfoList + 4 + i * 4);
      const u32 offset = sdat.infoOffset + relative;
      if (relative != 0 && reader.has(offset, 2)) {
        info.valid = true;
        info.fileId = reader.le16(offset);
      }
    }
    sdat.waveArchives.push_back(info);
  }

  return sdat;
}

[[nodiscard]] std::optional<FileRange> fileRange(ByteReader reader, const SdatInfo& sdat, u16 fileId) {
  const u64 fatEntry = static_cast<u64>(sdat.fatOffset) + 12 + static_cast<u64>(fileId) * 0x10;
  if (!reader.has(fatEntry, 8)) {
    return std::nullopt;
  }

  const u32 offset = reader.le32(fatEntry) + sdat.baseOffset;
  const u32 size = reader.le32(fatEntry + 4);
  if (!reader.has(offset, size)) {
    return std::nullopt;
  }

  return FileRange{.offset = offset, .size = size};
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

[[nodiscard]] Envelope ndsEnvelope(ByteReader reader, u32 offset) {
  // NDS envelopes use driver rate tables rather than SF2/DLS units. Preserve both rounded
  // microseconds and precise seconds so exporters can choose the most accurate conversion.
  const u8 attackTime = reader.u8At(offset + 1);
  const u8 decayTime = reader.u8At(offset + 2);
  const u8 sustainLevel = reader.u8At(offset + 3);
  const u8 releaseTime = reader.u8At(offset + 4);

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
  const double releaseSeconds = releaseTime == 0x7f ? -1.0 : ((0x16980 / realRelease) * kEnvelopeIntervalSeconds);

  return Envelope{
      .attack = envelopeMicros(attackSeconds),
      .decay = envelopeMicros(decaySeconds),
      .sustain = envelopePermille(sustainAmplitude),
      .release = envelopeMicros(releaseSeconds),
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

[[nodiscard]] Region ndsRegion(ByteReader reader, u32 offset, u32 length, u32 sampleIndex,
                               std::optional<AssetId> sampleCollection, u8 keyLow = 0, u8 keyHigh = 127,
                               std::optional<u8> forcedRootKey = std::nullopt) {
  const u32 articulationOffset = offset + length - 6;
  Region region{
      .keyRange = KeyRange{.low = keyLow, .high = keyHigh},
      .velocityRange = VelocityRange{.low = 0, .high = 127},
      .sample = SampleRef{.collection = sampleCollection, .index = sampleIndex},
      .range = reader.range(offset, length),
      .rootKey = forcedRootKey ? forcedRootKey : std::optional<u8>{reader.u8At(articulationOffset)},
      .envelope = ndsEnvelope(reader, articulationOffset),
      .pan = ndsPan(reader.u8At(articulationOffset + 5)),
  };
  return region;
}

[[nodiscard]] SampleCollectionAsset parsePsgSamples(const ScanInput& input, AssetId id) {
  // PSG wave/noise instruments do not reference SWAR sample data. Emit a synthetic sample
  // collection so they can still participate in the same Instrument/Region model.
  SampleCollectionAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kFormatName),
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

[[nodiscard]] SampleCollectionAsset parseWaveArchive(const ScanInput& input, AssetId id, FileRange range,
                                                     const std::string& name) {
  // SWAR samples are compact wave records. ADPCM entries point encodedData after their
  // four-byte predictor header; SampleDecoder knows to read that header.
  SampleCollectionAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kFormatName),
              .name = name,
              .range = input.reader.range(range.offset, range.size),
          },
  };
  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root =
      items.add(std::nullopt, ItemKind::SampleCollection, "swar", name, input.reader.range(range.offset, range.size));

  if (!matches(input.reader, range.offset, kSwarSignature) || !input.reader.has(range.offset + 0x3c, 4)) {
    return asset;
  }

  const u32 sampleCount = input.reader.le32(range.offset + 0x38);
  for (u32 i = 0; i < sampleCount; ++i) {
    if (!input.reader.has(range.offset + 0x3c + i * 4, 4)) {
      break;
    }
    const u32 sampleOffset = input.reader.le32(range.offset + 0x3c + i * 4) + range.offset;
    if (!input.reader.has(sampleOffset, 0x0c)) {
      continue;
    }

    const u8 waveType = input.reader.u8At(sampleOffset);
    const bool loops = input.reader.u8At(sampleOffset + 1) != 0;
    u32 sampleRate = input.reader.le16(sampleOffset + 2);
    const u16 timerValue = input.reader.le16(sampleOffset + 4);
    if (timerValue > 0) {
      sampleRate = 16756991 / timerValue;
    }

    u32 loopOffset = input.reader.le16(sampleOffset + 6) * 4;
    u32 nonLoopLength = input.reader.le16(sampleOffset + 8) * 4;
    u32 dataStart = sampleOffset + 0x0c;
    u32 dataLength = loopOffset + nonLoopLength;
    AudioCodec codec = AudioCodec::PcmS16;
    u16 bitsPerSample = 16;

    if (waveType == 0) {
      codec = AudioCodec::PcmS8;
      bitsPerSample = 8;
    } else if (waveType == 2) {
      codec = AudioCodec::NdsImaAdpcm;
      dataStart = sampleOffset + 0x10;
      dataLength = loopOffset + nonLoopLength - 4;
      loopOffset = loopOffset * 2 - 8 + 1;
      nonLoopLength = (dataLength * 2 + 1) - loopOffset;
    }

    if (!input.reader.has(dataStart, dataLength)) {
      continue;
    }

    const u32 bytesPerFrame = waveType == 1 ? 2 : 1;
    const Loop loop =
        waveType == 2
            ? Loop{.enabled = loops, .start = loopOffset, .length = nonLoopLength}
            : Loop{.enabled = loops, .start = loopOffset / bytesPerFrame, .length = nonLoopLength / bytesPerFrame};
    const std::string sampleName = fmt::format("Sample {}", asset.samples.samples.size());
    asset.samples.samples.push_back(Sample{
        .name = sampleName,
        .codec = codec,
        .encodedData = input.reader.range(dataStart, dataLength),
        .sampleRate = sampleRate,
        .channels = 1,
        .bitsPerSample = bitsPerSample,
        .loop = loop,
    });
    static_cast<void>(items.add(root, ItemKind::Sample, "swar-sample", sampleName,
                                input.reader.range(sampleOffset, dataStart + dataLength - sampleOffset)));
  }

  return asset;
}

[[nodiscard]] std::optional<AssetId> bankWaveCollection(const std::array<std::optional<AssetId>, 4>& collections,
                                                        u16 index) {
  if (index >= collections.size()) {
    return std::nullopt;
  }
  return collections[index];
}

[[nodiscard]] InstrumentSetAsset parseInstrumentSet(const ScanInput& input, AssetId id, FileRange range,
                                                    const std::string& name, AssetId psgCollection,
                                                    const std::array<std::optional<AssetId>, 4>& waveCollections) {
  // SBNK instruments fan out by type: single region, PSG pulse/noise, drumset, or
  // key-split multi-region. Each case fills the same value Instrument shape.
  InstrumentSetAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kFormatName),
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
        instrument.regions.push_back(ndsRegion(input.reader, instrumentOffset, 10, sampleIndex,
                                               bankWaveCollection(waveCollections, collectionIndex)));
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
        instrument.regions.push_back(
            ndsRegion(input.reader, instrumentOffset, 10, dutyCycle, psgCollection, 0, 127, 69));
        break;
      }
      case 0x03: {
        if (!input.reader.has(instrumentOffset, 10)) {
          break;
        }
        instrument.name = "PSG Noise";
        instrument.range = input.reader.range(instrumentOffset, 10);
        instrument.regions.push_back(ndsRegion(input.reader, instrumentOffset, 10, 8, psgCollection, 0, 127, 45));
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
          instrument.regions.push_back(ndsRegion(input.reader, regionOffset, 12, sampleIndex,
                                                 bankWaveCollection(waveCollections, collectionIndex), key, key));
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
          instrument.regions.push_back(ndsRegion(input.reader, regionOffset, 12, sampleIndex,
                                                 bankWaveCollection(waveCollections, collectionIndex), keyLow,
                                                 keyRanges[r]));
        }
        break;
      }
      default:
        break;
    }

    if (!instrument.regions.empty()) {
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

[[nodiscard]] std::vector<u32> findSdatOffsets(ByteReader reader) {
  std::vector<u32> offsets;
  for (u64 offset = 0; offset + kSdatSignature.size() <= reader.size(); ++offset) {
    if (matches(reader, offset, kSdatSignature) && reader.has(offset + 0x10, 4) &&
        reader.le32(offset + 0x10) < 0x10000) {
      offsets.push_back(static_cast<u32>(offset));
    }
  }
  return offsets;
}

void scanSdat(const ScanInput& input, const SdatInfo& sdat, ScanResult& result) {
  // Build dependencies before dependents: PSG samples are universal, SWAR collections feed
  // banks, banks feed sequences, and sequences finally become exportable collections.
  const auto psgId = input.ids.nextAssetId();
  result.assets.emplace_back(parsePsgSamples(input, psgId));

  std::vector<std::optional<AssetId>> waveAssetIds(sdat.waveArchives.size());
  std::set<u16> referencedWaveArchives;
  for (const auto& bank : sdat.banks) {
    for (const u16 waveArchive : bank.waveArchives) {
      if (waveArchive != 0xffff && waveArchive < sdat.waveArchives.size()) {
        referencedWaveArchives.insert(waveArchive);
      }
    }
  }

  for (const u16 waveArchiveIndex : referencedWaveArchives) {
    const auto& waveArchive = sdat.waveArchives[waveArchiveIndex];
    if (!waveArchive.valid) {
      continue;
    }
    const auto range = fileRange(input.reader, sdat, waveArchive.fileId);
    if (!range) {
      result.diagnostics.push_back(
          warning("NDS wave archive FAT entry was invalid", input.reader.range(sdat.baseOffset, sdat.length)));
      continue;
    }
    if (!matches(input.reader, range->offset, kSwarSignature)) {
      continue;
    }
    const auto id = input.ids.nextAssetId();
    waveAssetIds[waveArchiveIndex] = id;
    result.assets.emplace_back(parseWaveArchive(input, id, *range, sdat.waveArchiveNames[waveArchiveIndex]));
  }

  std::map<u16, AssetId> bankAssetIds;
  std::set<u16> referencedBanks;
  for (const auto& sequence : sdat.sequences) {
    if (sequence.valid && sequence.bank < sdat.banks.size()) {
      referencedBanks.insert(sequence.bank);
    }
  }

  for (const u16 bankIndex : referencedBanks) {
    const auto& bank = sdat.banks[bankIndex];
    if (!bank.valid) {
      continue;
    }
    const auto range = fileRange(input.reader, sdat, bank.fileId);
    if (!range) {
      result.diagnostics.push_back(
          warning("NDS instrument bank FAT entry was invalid", input.reader.range(sdat.baseOffset, sdat.length)));
      continue;
    }

    std::array<std::optional<AssetId>, 4> waveCollections{};
    for (u32 i = 0; i < bank.waveArchives.size(); ++i) {
      const u16 waveArchive = bank.waveArchives[i];
      if (waveArchive != 0xffff && waveArchive < waveAssetIds.size()) {
        waveCollections[i] = waveAssetIds[waveArchive];
      }
    }

    const auto id = input.ids.nextAssetId();
    bankAssetIds.emplace(bankIndex, id);
    result.assets.emplace_back(
        parseInstrumentSet(input, id, *range, sdat.bankNames[bankIndex], psgId, waveCollections));
  }

  for (u32 sequenceIndex = 0; sequenceIndex < sdat.sequences.size(); ++sequenceIndex) {
    const auto& sequence = sdat.sequences[sequenceIndex];
    if (!sequence.valid) {
      continue;
    }
    const auto sequenceRange = fileRange(input.reader, sdat, sequence.fileId);
    if (!sequenceRange) {
      result.diagnostics.push_back(
          warning("NDS sequence FAT entry was invalid", input.reader.range(sdat.baseOffset, sdat.length)));
      continue;
    }

    const auto bankAsset = bankAssetIds.find(sequence.bank);
    const std::optional<AssetId> instrumentSet =
        bankAsset == bankAssetIds.end() ? std::nullopt : std::optional<AssetId>{bankAsset->second};
    const auto sequenceId = input.ids.nextAssetId();
    const std::string& name = sdat.sequenceNames[sequenceIndex];
    const bool hasSseqHeader = matches(input.reader, sequenceRange->offset, kSseqSignature);
    const bool parseMalformedFallthrough =
        !hasSseqHeader && shouldParseMalformedFallthrough(input.reader, *sequenceRange);
    const bool extendMalformedPastFatRange = parseMalformedFallthrough && sequenceRange->size <= 0x100;
    const u32 sequenceEnd =
        hasSseqHeader || !extendMalformedPastFatRange
            ? static_cast<u32>(
                  std::min<u64>(input.reader.size(), static_cast<u64>(sequenceRange->offset) + sequenceRange->size))
            : static_cast<u32>(input.reader.size());
    result.assets.emplace_back(parseNdsSequenceProgram(input, sequenceId,
                                                       NdsSequenceRange{
                                                           .offset = sequenceRange->offset,
                                                           .size = sequenceRange->size,
                                                           .sequenceEnd = sequenceEnd,
                                                           .linearizeMalformedControlFlow = parseMalformedFallthrough,
                                                       },
                                                       name, instrumentSet));

    Collection collection{
        .id = input.ids.nextCollectionId(),
        .name = name,
        .sequence = sequenceId,
    };
    collection.sampleCollections.push_back(psgId);
    if (instrumentSet) {
      collection.instrumentSets.push_back(*instrumentSet);
    }
    if (sequence.bank < sdat.banks.size()) {
      for (const u16 waveArchive : sdat.banks[sequence.bank].waveArchives) {
        if (waveArchive != 0xffff && waveArchive < waveAssetIds.size() && waveAssetIds[waveArchive]) {
          collection.sampleCollections.push_back(*waveAssetIds[waveArchive]);
        }
      }
    }
    result.collections.push_back(std::move(collection));
  }
}

}  // namespace

[[nodiscard]] bool canScanNds(const SourceFile&, std::span<const u8> bytes) {
  return !findSdatOffsets(ByteReader(SourceId{}, bytes)).empty();
}

[[nodiscard]] ScanResult scanNds(const ScanInput& input) {
  ScanResult result;
  for (const u32 offset : findSdatOffsets(input.reader)) {
    const auto sdat = parseSdatInfo(input.reader, offset);
    if (!sdat) {
      result.diagnostics.push_back(warning("NDS SDAT header was invalid", input.reader.range(offset, 0x24)));
      continue;
    }
    scanSdat(input, *sdat, result);
  }
  return result;
}

void registerNdsModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = kFormatName,
      .canScan = canScanNds,
      .scan = scanNds,
  });
}

}  // namespace vgmtrans::formats::nds

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnesSynth.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace vgmtrans::formats::akao_snes {

using namespace core;

namespace {

struct AkaoSnesInstrumentInfo {
  u8 srcn = 0;
  u32 tuningAddress = 0;
  u32 adsrAddress = 0;
  u8 tuning1 = 0;
  u8 tuning2 = 0;
  u8 adsr1 = 0xff;
  u8 adsr2 = 0xe0;
  bool percussion = false;
  u8 percussionIndex = 0;
  u8 percussionKey = 0;
  std::optional<u8> percussionPan;
};

constexpr u8 kMinTimer0Frequency = 0x24;
constexpr u8 kMaxTimer0Frequency = 0x2a;

[[nodiscard]] double frameRateHz(u8 timer0Frequency) {
  return 8000.0 / timer0Frequency;
}

[[nodiscard]] double minLfoRateHz(AkaoSnesVersion) {
  // Match legacy's current export floor while BASS MIDI remains the practical limiter.
  return 1.0 / 16.0;
}

[[nodiscard]] double maxLfoRateHz(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 2.0);
  }
  if (version == AKAOSNES_V2) {
    return frameRateHz(kMinTimer0Frequency) / 2.0;
  }
  return 8000.0 / kMinTimer0Frequency / 2.0;
}

[[nodiscard]] double vibratoDepthCentsForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }
  const double ratio = 15.0 * amplitude / 32768.0;
  const double centsUp = 1200.0 * std::log2(1.0 + ratio);
  const double centsDown = -1200.0 * std::log2(1.0 - ratio);
  return std::max(centsUp, centsDown);
}

[[nodiscard]] double v1VibratoDepthCentsForHighByte(u8 amplitude) {
  if (amplitude == 0) {
    return 0.0;
  }
  return 1200.0 * std::log2(1.0 + (static_cast<double>(amplitude) / 3072.0));
}

[[nodiscard]] double maxVibratoDepthCents(AkaoSnesVersion version) {
  switch (version) {
    case AKAOSNES_V1:
      return v1VibratoDepthCentsForHighByte(255);
    case AKAOSNES_V2:
      return 1200.0 * std::log2(1.0 + (15.0 * 127.0 / 32768.0));
    case AKAOSNES_V3:
      return vibratoDepthCentsForAmplitude(127.0);
    case AKAOSNES_V4:
    default:
      return vibratoDepthCentsForAmplitude(64.0);
  }
}

[[nodiscard]] double tremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }
  const double troughScale = std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0));
  return -20.0 * std::log10(troughScale);
}

[[nodiscard]] bool exportsTremolo(AkaoSnesVersion version) {
  return version == AKAOSNES_V3 || version == AKAOSNES_V4;
}

[[nodiscard]] double maxTremoloDepthDb(AkaoSnesVersion version) {
  if (version == AKAOSNES_V3) {
    return tremoloDepthDbForAmplitude(127.0);
  }
  if (version == AKAOSNES_V4) {
    return tremoloDepthDbForAmplitude(64.0);
  }
  return 0.0;
}

[[nodiscard]] double maxDelaySeconds(AkaoSnesVersion version) {
  constexpr double maxV1DelaySeconds = 254.0 * 256.0 / (8000.0 / kMinTimer0Frequency);
  constexpr double maxV4DelaySeconds = 254.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);
  constexpr double maxDelaySeconds = 255.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);
  if (version == AKAOSNES_V1) {
    return maxV1DelaySeconds;
  }
  return version == AKAOSNES_V4 ? maxV4DelaySeconds : maxDelaySeconds;
}

[[nodiscard]] InstrumentModulation akaoInstrumentModulation(AkaoSnesVersion version) {
  InstrumentModulation modulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = maxVibratoDepthCents(version),
              .rateHertz = {minLfoRateHz(version), maxLfoRateHz(version)},
              .delaySeconds = ModulationRange{0.0, maxDelaySeconds(version)},
          },
  };
  if (exportsTremolo(version)) {
    modulation.tremolo = TremoloSpec{
        .maxDepthDb = maxTremoloDepthDb(version),
        .rateHertz = {minLfoRateHz(version), maxLfoRateHz(version)},
        .delaySeconds = ModulationRange{0.0, maxDelaySeconds(version)},
    };
  }
  return modulation;
}

[[nodiscard]] std::optional<AkaoSnesInstrumentInfo> melodicInstrumentInfo(ByteReader reader,
                                                                          const AkaoSnesLayout& layout, u8 srcn) {
  if (!layout.spcDirAddress || !layout.tuningTableAddress) {
    return std::nullopt;
  }

  const u32 dirEntry = *layout.spcDirAddress + srcn * 4;
  if (!readSnesSampleDirectoryEntry(reader, dirEntry)) {
    return std::nullopt;
  }

  const u16 sampleStart = reader.le16(dirEntry);
  const u16 instrumentMinOffset = layout.version == AKAOSNES_V4 ? 0x200 : static_cast<u16>(*layout.spcDirAddress);
  if (sampleStart < instrumentMinOffset) {
    return std::nullopt;
  }

  const u32 tuningAddress = (layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2)
                                ? *layout.tuningTableAddress + srcn
                                : *layout.tuningTableAddress + srcn * 2;
  if (!reader.has(tuningAddress, layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2 ? 1 : 2)) {
    return std::nullopt;
  }

  u8 adsr1 = 0xff;
  u8 adsr2 = 0xe0;
  u32 adsrAddress = 0;
  if (layout.version != AKAOSNES_V1) {
    if (!layout.adsrTableAddress) {
      return std::nullopt;
    }
    adsrAddress = *layout.adsrTableAddress + srcn * 2;
    if (!reader.has(adsrAddress, 2)) {
      return std::nullopt;
    }
    if (reader.le16(adsrAddress) == 0x0000) {
      return std::nullopt;
    }
    adsr1 = reader.u8At(adsrAddress);
    adsr2 = reader.u8At(adsrAddress + 1);
  }

  return AkaoSnesInstrumentInfo{
      .srcn = srcn,
      .tuningAddress = tuningAddress,
      .adsrAddress = adsrAddress,
      .tuning1 = reader.u8At(tuningAddress),
      .tuning2 =
          (layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2) ? u8{0} : reader.u8At(tuningAddress + 1),
      .adsr1 = adsr1,
      .adsr2 = adsr2,
  };
}

struct AkaoPitch {
  u8 rootKey = 69;
  s16 fineTuneCents = 0;
  Tuning aggregate;
};

[[nodiscard]] AkaoPitch akaoPitch(const AkaoSnesInstrumentInfo& info) {
  double pitchScale = 0.0;
  if (info.tuning1 <= 0x7f) {
    pitchScale = 1.0 + (static_cast<double>(info.tuning1) / 256.0);
  } else {
    pitchScale = static_cast<double>(info.tuning1) / 256.0;
  }
  pitchScale += static_cast<double>(info.tuning2) / 65536.0;

  double coarse = 0.0;
  double fine = std::modf((std::log(pitchScale) / std::log(2.0)) * 12.0, &coarse);
  if (fine >= 0.5) {
    coarse += 1.0;
    fine -= 1.0;
  } else if (fine <= -0.5) {
    coarse -= 1.0;
    fine += 1.0;
  }

  int root = 69 - static_cast<int>(coarse);
  if (info.percussion) {
    root = root + kAkaoSnesDrumKeyBias - info.percussionKey + info.percussionIndex;
  }

  const s16 fineTune = static_cast<s16>(fine * 100.0);
  return AkaoPitch{
      .rootKey = static_cast<u8>(std::clamp(root, 0, 127)),
      .fineTuneCents = fineTune,
      .aggregate = Tuning{.cents = static_cast<s32>((root - 69) * 100 + fineTune)},
  };
}

[[nodiscard]] SourceRange instrumentObjectRange(ByteReader reader, const AkaoSnesLayout& layout) {
  const u32 tuningSize = (layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2) ? 1 : 2;
  return reader.range(*layout.tuningTableAddress, tuningSize);
}

[[nodiscard]] SourceRange instrumentInfoRowRange(ByteReader reader, const AkaoSnesInstrumentInfo& info,
                                                 AkaoSnesVersion version) {
  const u32 tuningSize = (version == AKAOSNES_V1 || version == AKAOSNES_V2) ? 1 : 2;
  return reader.range(info.tuningAddress, tuningSize);
}

}  // namespace

std::vector<AkaoSnesInstrumentInfo> parseAkaoSnesInstrumentInfos(ByteReader reader, const AkaoSnesLayout& layout) {
  std::vector<AkaoSnesInstrumentInfo> infos;
  if (!layout.spcDirAddress || !layout.tuningTableAddress) {
    return infos;
  }

  const u8 maxSrcn = layout.version == AKAOSNES_V1 ? 0x7f : 0x3f;
  for (u8 srcn = 0; srcn <= maxSrcn; ++srcn) {
    const u32 dirEntry = *layout.spcDirAddress + srcn * 4;
    if (!readSnesSampleDirectoryEntry(reader, dirEntry)) {
      continue;
    }

    const u16 sampleStart = reader.le16(dirEntry);
    const u16 instrumentMinOffset = layout.version == AKAOSNES_V4 ? 0x200 : static_cast<u16>(*layout.spcDirAddress);
    if (sampleStart < instrumentMinOffset) {
      continue;
    }

    const bool shortTuning = layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2;
    const u32 tuningAddress = shortTuning ? *layout.tuningTableAddress + srcn : *layout.tuningTableAddress + srcn * 2;
    if (!reader.has(tuningAddress, shortTuning ? 1 : 2)) {
      break;
    }

    u8 adsr1 = 0xff;
    u8 adsr2 = 0xe0;
    u32 adsrAddress = 0;
    if (layout.version != AKAOSNES_V1) {
      if (!layout.adsrTableAddress) {
        break;
      }
      adsrAddress = *layout.adsrTableAddress + srcn * 2;
      if (!reader.has(adsrAddress, 2) || reader.le16(adsrAddress) == 0x0000) {
        break;
      }
      adsr1 = reader.u8At(adsrAddress);
      adsr2 = reader.u8At(adsrAddress + 1);
    }

    infos.push_back(AkaoSnesInstrumentInfo{
        .srcn = srcn,
        .tuningAddress = tuningAddress,
        .adsrAddress = adsrAddress,
        .tuning1 = reader.u8At(tuningAddress),
        .tuning2 = shortTuning ? u8{0} : reader.u8At(tuningAddress + 1),
        .adsr1 = adsr1,
        .adsr2 = adsr2,
    });
  }

  if (layout.percussionTableAddress) {
    for (u8 percussionIndex = 0; percussionIndex < akaoSnesNoteDurationTableSize(layout.version); ++percussionIndex) {
      const u32 row = *layout.percussionTableAddress + percussionIndex * 3;
      if (!reader.has(row, 3)) {
        break;
      }
      const u8 instrumentIndex = reader.u8At(row);
      if (instrumentIndex == 0 || instrumentIndex == 0xff) {
        continue;
      }
      auto info = melodicInstrumentInfo(reader, layout, instrumentIndex);
      if (!info) {
        continue;
      }
      info->percussion = true;
      info->percussionIndex = percussionIndex;
      info->percussionKey = reader.u8At(row + 1);
      const u8 pan = reader.u8At(row + 2);
      if (pan < 0x80) {
        info->percussionPan = pan;
      }
      infos.push_back(*info);
    }
  }

  return infos;
}

SnesBrrCatalog readAkaoSnesSamples(ByteReader reader, u32 spcDirAddress,
                                   const std::vector<AkaoSnesInstrumentInfo>& instruments) {
  std::vector<u8> srcns;
  srcns.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    srcns.push_back(instrument.srcn);
  }
  return readSnesBrrCatalog(reader, spcDirAddress, srcns);
}

struct AkaoSnesInstrumentBuild {
  SourceRange range;
  std::vector<Instrument> instruments;
};

AkaoSnesInstrumentBuild buildAkaoSnesInstruments(const ScanInput& input, ScanResultBuilder& builder,
                                                 ScanInstrumentSetRef instrumentSet,
                                                 ScanSampleCollectionRef sampleCollection, const AkaoSnesLayout& layout,
                                                 const std::vector<AkaoSnesInstrumentInfo>& instrumentInfos,
                                                 const SnesBrrCatalog& samples) {
  u32 rootOffset = instrumentInfos.empty() ? 0 : instrumentInfos.front().tuningAddress;
  u32 rootEnd = rootOffset;
  for (const auto& info : instrumentInfos) {
    rootOffset = std::min(rootOffset, info.tuningAddress);
    rootEnd = std::max(rootEnd,
                       info.tuningAddress + (layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2 ? 1u : 2u));
    if (info.adsrAddress != 0) {
      rootOffset = std::min(rootOffset, info.adsrAddress);
      rootEnd = std::max(rootEnd, info.adsrAddress + 2);
    }
  }
  const u32 rootSize = rootEnd >= rootOffset ? rootEnd - rootOffset : 0;
  const SourceAnnotationId root = builder.sourceMap()
                                      .table("Instrument Tables", input.reader.range(rootOffset, rootSize))
                                      .kind("akao-snes-instrument-tables")
                                      .owner(ObjectRefs::asset(instrumentSet.id))
                                      .id();

  std::map<u32, size_t> instrumentIndexByProgram;
  std::vector<Instrument> instruments;
  for (const auto& info : instrumentInfos) {
    const auto sampleIndex = samples.canonicalIndex(info.srcn);
    if (!sampleIndex) {
      continue;
    }

    const u32 bank = info.percussion ? kAkaoSnesDrumKitBank : 0;
    const u32 program = info.percussion ? kAkaoSnesDrumKitProgram : info.srcn;
    const u32 programKey = (bank << 7) | program;
    size_t instrumentIndex = 0;
    if (const auto found = instrumentIndexByProgram.find(programKey); found != instrumentIndexByProgram.end()) {
      instrumentIndex = found->second;
    } else {
      instrumentIndex = instruments.size();
      instrumentIndexByProgram.emplace(programKey, instrumentIndex);
      instruments.push_back(Instrument{
          .explicitAddress = InstrumentAddress{.bank = bank, .program = program},
          .name = info.percussion ? "Drum Kit" : fmt::format("Instrument {}", static_cast<unsigned>(info.srcn)),
          .range = instrumentObjectRange(input.reader, layout),
          .modulation = akaoInstrumentModulation(layout.version),
      });
    }
    const auto pitch = akaoPitch(info);
    Region region{
        .sample = builder.sampleRef(sampleCollection, *sampleIndex),
        .range = instrumentObjectRange(input.reader, layout),
        .tuning = pitch.aggregate,
        .rootKey = pitch.rootKey,
        .fineTuneCents = pitch.fineTuneCents,
        .envelope = (info.adsr1 & 0x80) != 0 ? snesDspEnvelope(info.adsr1, info.adsr2, 0xa0) : Envelope{},
        .pan = info.percussionPan ? std::clamp(static_cast<double>(*info.percussionPan) / 127.0, 0.0, 1.0) : 0.5,
    };
    if (info.percussion) {
      region.keyRange = KeyRange{.low = static_cast<u8>(info.percussionIndex + kAkaoSnesDrumKeyBias),
                                 .high = static_cast<u8>(info.percussionIndex + kAkaoSnesDrumKeyBias)};
    }
    instruments[instrumentIndex].regions.push_back(std::move(region));

    auto annotation =
        builder.sourceMap()
            .row(info.percussion ? fmt::format("Percussion {}", static_cast<unsigned>(info.percussionIndex))
                                 : fmt::format("Instrument {}", static_cast<unsigned>(info.srcn)),
                 instrumentInfoRowRange(input.reader, info, layout.version))
            .role(SourceRole::Instrument)
            .kind(info.percussion ? "akao-snes-percussion-instrument" : "akao-snes-instrument")
            .owner(ObjectRefs::instrument(instrumentSet.id, programKey))
            .derived("bank", bank)
            .derived("program", program)
            .field("tuning",
                   input.reader.range(info.tuningAddress,
                                      layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2 ? 1 : 2),
                   info.tuning1, SourceValueDisplay::Hex);
    if (info.adsrAddress != 0) {
      annotation.field("adsr1", input.reader.range(info.adsrAddress, 1), info.adsr1, SourceValueDisplay::Hex)
          .field("adsr2", input.reader.range(info.adsrAddress + 1, 1), info.adsr2, SourceValueDisplay::Hex);
    }
    annotation.parent(root).link(SourceLinkRole::UsesSample,
                                 SourceTarget{ObjectRefs::sample(sampleCollection.id, *sampleIndex)});
  }

  return AkaoSnesInstrumentBuild{
      .range = input.reader.range(rootOffset, rootSize),
      .instruments = std::move(instruments),
  };
}

bool addAkaoSnesSynth(const ScanInput& input, ScanResultBuilder& builder, ScanInstrumentSetRef instrumentSet,
                      ScanSampleCollectionRef sampleCollection, const AkaoSnesLayout& layout,
                      std::string_view displayName) {
  const auto instrumentInfos = parseAkaoSnesInstrumentInfos(input.reader, layout);
  const auto samples = readAkaoSnesSamples(input.reader, *layout.spcDirAddress, instrumentInfos);
  if (instrumentInfos.empty() || samples.samples.empty()) {
    return false;
  }

  auto instruments =
      buildAkaoSnesInstruments(input, builder, instrumentSet, sampleCollection, layout, instrumentInfos, samples);
  builder.instrumentSet(instrumentSet, fmt::format("{} Instruments", displayName), instruments.range)
      .instruments(std::move(instruments.instruments));
  builder.sampleCollection(sampleCollection, fmt::format("{} Samples", displayName), samples.directoryRange)
      .samples(buildSnesBrrSampleCollection(input.reader, samples, sampleCollection.id, builder.sourceMap(),
                                            "akao-snes-sample-dir-entry"));
  return true;
}

}  // namespace vgmtrans::formats::akao_snes

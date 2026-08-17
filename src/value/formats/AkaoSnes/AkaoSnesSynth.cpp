/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnes.h"

#include "value/base/RecordReader.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace vgmtrans::formats::akao_snes {

using namespace core;

namespace {

struct AkaoSnesInstrumentInfo {
  u8 srcn = 0;
  u8 tuning1 = 0;
  u8 tuning2 = 0;
  u8 adsr1 = 0xff;
  u8 adsr2 = 0xe0;
  SourceRecord tuning;
  std::optional<SourceRecord> adsr;
  std::optional<SourceRecord> percussionSource;
  bool percussion = false;
  u8 percussionIndex = 0;
  u8 percussionKey = 0;
  std::optional<u8> percussionPan;
};

enum class InstrumentReadStatus {
  Valid,
  Skip,
  Stop,
};

struct InstrumentReadResult {
  InstrumentReadStatus status = InstrumentReadStatus::Skip;
  std::optional<AkaoSnesInstrumentInfo> info;
};

[[nodiscard]] InstrumentReadResult readMelodicInstrument(ByteReader reader, const AkaoSnesLayout& layout, u8 srcn,
                                                         std::vector<Diagnostic>* diagnostics) {
  if (!layout.spcDirAddress || !layout.tuningTableAddress) {
    return {.status = InstrumentReadStatus::Stop};
  }

  const u32 dirEntry = *layout.spcDirAddress + srcn * 4;
  if (!readSnesSampleDirectoryEntry(reader, dirEntry)) {
    return {.status = InstrumentReadStatus::Skip};
  }

  const u16 sampleStart = reader.le16(dirEntry);
  const u16 instrumentMinOffset = layout.version == AKAOSNES_V4 ? 0x200 : static_cast<u16>(*layout.spcDirAddress);
  if (sampleStart < instrumentMinOffset) {
    return {.status = InstrumentReadStatus::Skip};
  }

  const u32 tuningAddress = (layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2)
                                ? *layout.tuningTableAddress + srcn
                                : *layout.tuningTableAddress + srcn * 2;
  const bool shortTuning = layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2;
  const u32 tuningSize = shortTuning ? 1 : 2;
  if (!reader.has(tuningAddress, tuningSize)) {
    return {.status = InstrumentReadStatus::Stop};
  }

  RecordReader tuningReader(reader, tuningAddress, tuningAddress + tuningSize, diagnostics);
  const auto tuning1 = tuningReader.u8(shortTuning ? "tuning" : "tuning_high", SourceValueDisplay::Hex);
  const auto tuning2 = shortTuning ? RangedValue<u8>{} : tuningReader.u8("tuning_low", SourceValueDisplay::Hex);
  if (!tuning1 || (!shortTuning && !tuning2)) {
    return {.status = InstrumentReadStatus::Stop};
  }

  u8 adsr1 = 0xff;
  u8 adsr2 = 0xe0;
  std::optional<SourceRecord> adsr;
  if (layout.version != AKAOSNES_V1) {
    if (!layout.adsrTableAddress) {
      return {.status = InstrumentReadStatus::Stop};
    }
    const u32 adsrAddress = *layout.adsrTableAddress + srcn * 2;
    if (!reader.has(adsrAddress, 2)) {
      return {.status = InstrumentReadStatus::Stop};
    }
    if (reader.le16(adsrAddress) == 0x0000) {
      return {.status = InstrumentReadStatus::Stop};
    }
    RecordReader adsrReader(reader, adsrAddress, adsrAddress + 2, diagnostics);
    const auto first = adsrReader.u8("adsr1", SourceValueDisplay::Hex);
    const auto second = adsrReader.u8("adsr2", SourceValueDisplay::Hex);
    if (!first || !second) {
      return {.status = InstrumentReadStatus::Stop};
    }
    adsr1 = first.value;
    adsr2 = second.value;
    adsr = std::move(adsrReader).finish();
  }

  return InstrumentReadResult{
      .status = InstrumentReadStatus::Valid,
      .info =
          AkaoSnesInstrumentInfo{
              .srcn = srcn,
              .tuning1 = tuning1.value,
              .tuning2 = shortTuning ? u8{0} : tuning2.value,
              .adsr1 = adsr1,
              .adsr2 = adsr2,
              .tuning = std::move(tuningReader).finish(),
              .adsr = std::move(adsr),
          },
  };
}

[[nodiscard]] double akaoUnityKey(const AkaoSnesInstrumentInfo& info) {
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
  return std::clamp(root, 0, 127) - (fineTune / 100.0);
}

std::vector<AkaoSnesInstrumentInfo> parseAkaoSnesInstrumentInfos(ByteReader reader, const AkaoSnesLayout& layout,
                                                                 std::vector<Diagnostic>* diagnostics) {
  std::vector<AkaoSnesInstrumentInfo> infos;
  if (!layout.spcDirAddress || !layout.tuningTableAddress) {
    return infos;
  }

  std::array<std::optional<InstrumentReadResult>, 128> cache;
  const auto instrument = [&](u8 srcn) -> const InstrumentReadResult& {
    auto& cached = cache[srcn];
    if (!cached) {
      cached = readMelodicInstrument(reader, layout, srcn, diagnostics);
    }
    return *cached;
  };

  const u8 maxSrcn = layout.version == AKAOSNES_V1 ? 0x7f : 0x3f;
  for (u8 srcn = 0; srcn <= maxSrcn; ++srcn) {
    const auto& result = instrument(srcn);
    if (result.status == InstrumentReadStatus::Skip) {
      continue;
    }
    if (result.status == InstrumentReadStatus::Stop) {
      break;
    }
    infos.push_back(*result.info);
  }

  if (layout.percussionTableAddress) {
    for (u8 percussionIndex = 0; percussionIndex < akaoSnesNoteDurationTableSize(layout.version); ++percussionIndex) {
      const u32 row = *layout.percussionTableAddress + percussionIndex * 3;
      if (!reader.has(row, 3)) {
        break;
      }
      RecordReader percussionReader(reader, row, row + 3, diagnostics);
      const auto instrumentIndex = percussionReader.u8("instrument");
      const auto key = percussionReader.u8("key", SourceValueDisplay::MidiNote);
      const auto pan = percussionReader.u8("pan", SourceValueDisplay::Hex);
      if (!instrumentIndex || !key || !pan) {
        break;
      }
      if (instrumentIndex.value == 0 || instrumentIndex.value == 0xff) {
        continue;
      }
      const auto& melodic = instrument(instrumentIndex.value);
      if (melodic.status != InstrumentReadStatus::Valid) {
        continue;
      }
      auto info = *melodic.info;
      info.percussion = true;
      info.percussionSource = std::move(percussionReader).finish();
      info.percussionIndex = percussionIndex;
      info.percussionKey = key.value;
      if (pan.value < 0x80) {
        info.percussionPan = pan.value;
      }
      infos.push_back(std::move(info));
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

void addAkaoSnesInstruments(InstrumentSetBuilder& instruments, ByteReader reader, const AkaoSnesLayout& layout,
                            const std::vector<AkaoSnesInstrumentInfo>& instrumentInfos,
                            const SnesBrrSampleRefs& sampleRefs) {
  const u8 highest = std::ranges::max(instrumentInfos, {}, &AkaoSnesInstrumentInfo::srcn).srcn;
  std::optional<SourceAnnotationId> tuningTable;
  if (layout.tuningTableAddress) {
    const u32 stride = layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2 ? 1 : 2;
    tuningTable = instruments
                      .source(SourceRole::Table, "Tuning Table",
                              reader.range(*layout.tuningTableAddress, (static_cast<u32>(highest) + 1) * stride),
                              "akao-snes-tuning-table")
                      .id();
  }
  std::optional<SourceAnnotationId> adsrTable;
  if (layout.adsrTableAddress) {
    adsrTable =
        instruments
            .source(SourceRole::Table, "ADSR Table",
                    reader.range(*layout.adsrTableAddress, (static_cast<u32>(highest) + 1) * 2), "akao-snes-adsr-table")
            .id();
  }
  std::optional<SourceAnnotationId> percussionTable;
  if (layout.percussionTableAddress) {
    percussionTable =
        instruments
            .source(SourceRole::Table, "Percussion Table",
                    reader.range(*layout.percussionTableAddress, akaoSnesNoteDurationTableSize(layout.version) * 3),
                    "akao-snes-percussion-table")
            .id();
  }

  for (const auto& info : instrumentInfos) {
    const auto sample = sampleRefs.findSrcn(info.srcn);
    if (!sample) {
      instruments.warning("Instrument sample was not found", info.tuning.range);
      continue;
    }

    const u32 bank = info.percussion ? kAkaoSnesDrumKitBank : 0;
    const u32 program = info.percussion ? kAkaoSnesDrumKitProgram : info.srcn;
    const u32 programKey = (bank << 7) | program;
    Instrument model{
        .explicitAddress = InstrumentAddress{.bank = bank, .program = program},
        .name = info.percussion ? "Drum Kit" : fmt::format("Instrument {}", static_cast<unsigned>(info.srcn)),
        .range = info.tuning.range,
    };
    auto instrument = info.percussion ? instruments.getOrAdd(programKey, std::move(model))
                                      : instruments.add(programKey, std::move(model));
    auto tuning = instrument.source("Tuning Entry", info.tuning, "akao-snes-tuning-entry");
    if (tuningTable) {
      tuning.parent(*tuningTable);
    }
    if (info.adsr) {
      auto adsr = instrument.source("ADSR Entry", *info.adsr, "akao-snes-adsr-entry");
      if (adsrTable) {
        adsr.parent(*adsrTable);
      }
    }

    const double unityKey = akaoUnityKey(info);
    Region region{
        .range = info.tuning.range,
        .unityKey = unityKey,
        .envelope = (info.adsr1 & 0x80) != 0 ? snesDspEnvelope(info.adsr1, info.adsr2, 0xa0) : Envelope{},
        .pan = info.percussionPan ? std::clamp(static_cast<double>(*info.percussionPan) / 127.0, 0.0, 1.0) : 0.5,
    };
    if (info.percussion) {
      region.keyRange = KeyRange{.low = static_cast<u8>(info.percussionIndex + kAkaoSnesDrumKeyBias),
                                 .high = static_cast<u8>(info.percussionIndex + kAkaoSnesDrumKeyBias)};
    }
    auto regionEntry = instrument.region(*sample, std::move(region));
    if (info.percussion) {
      auto percussion = regionEntry.source(fmt::format("Percussion {}", static_cast<unsigned>(info.percussionIndex)),
                                           *info.percussionSource, "akao-snes-percussion-entry");
      if (percussionTable) {
        percussion.parent(*percussionTable);
      }
    } else {
      regionEntry.source("Region", info.tuning.range, "akao-snes-region");
    }
  }
}

}  // namespace

std::optional<ScanSoundBankRef> addAkaoSnesSynth(ScanResultBuilder& builder, const AkaoSnesLayout& layout,
                                                 std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const auto instrumentInfos = parseAkaoSnesInstrumentInfos(reader, layout, &builder.diagnostics());
  if (instrumentInfos.empty() || !layout.spcDirAddress) {
    return std::nullopt;
  }
  const auto sampleCatalog = readAkaoSnesSamples(reader, *layout.spcDirAddress, instrumentInfos);
  if (sampleCatalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.soundBank(fmt::format("{} Instruments", displayName));
  const auto sampleRefs = addSnesBrrSamples(instruments.samples(), reader, sampleCatalog, "akao-snes-sample-dir-entry");

  addAkaoSnesInstruments(instruments.builder(), reader, layout, instrumentInfos, sampleRefs);

  return instruments.ref();
}

}  // namespace vgmtrans::formats::akao_snes

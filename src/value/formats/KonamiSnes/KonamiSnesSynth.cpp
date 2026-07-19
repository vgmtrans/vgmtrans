/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnesSynth.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace vgmtrans::formats::konami_snes {

using namespace core;

namespace {

constexpr u8 kPercussionNoteCount = 0x60;
constexpr u8 kPercussionBaseNote = 0x3c;
constexpr u32 kDrumKitBank = 0x7f;
constexpr u32 kDrumKitProgram = 0x00;

[[nodiscard]] bool usesLegacyPanRange(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 || version == KONAMISNES_V2;
}

[[nodiscard]] u8 percussionPanLimit(KonamiSnesVersion version) {
  return usesLegacyPanRange(version) ? 0x14 : 0x28;
}

[[nodiscard]] bool instrumentHeaderIsValid(ByteReader reader, KonamiSnesVersion version, u32 address, u32 spcDirAddress,
                                           bool validateSample) {
  const u32 headerSize = instrumentHeaderSize(version);
  if (!reader.has(address, headerSize)) {
    return false;
  }

  const u8 srcn = reader.u8At(address);
  if (srcn == 0xff) {
    return false;
  }

  const u32 dirEntryAddress = spcDirAddress + srcn * 4;
  if (!readSnesSampleDirectoryEntry(reader, dirEntryAddress, validateSample)) {
    return false;
  }

  const u16 sampleStart = reader.le16(dirEntryAddress);
  const u16 sampleLoop = reader.le16(dirEntryAddress + 2);
  return sampleStart <= sampleLoop && ((sampleLoop - sampleStart) % 9) == 0;
}

[[nodiscard]] bool percussionHeaderIsValid(ByteReader reader, KonamiSnesVersion version, u32 address,
                                           u32 spcDirAddress) {
  if (!instrumentHeaderIsValid(reader, version, address, spcDirAddress, true)) {
    return false;
  }
  const bool legacyLayout = usesLegacyInstrumentLayout(version);
  const u8 pan = reader.u8At(address + (legacyLayout ? 6 : 5));
  const u8 volume = reader.u8At(address + (legacyLayout ? 7 : 6));
  return pan <= percussionPanLimit(version) && volume <= 0x7f;
}

[[nodiscard]] int percussionKey(ByteReader reader, u32 address) {
  const s8 rawKey = reader.s8At(address + 1);
  const s8 tuning = reader.s8At(address + 2);
  return tuning >= 0 ? rawKey : rawKey - 1;
}

[[nodiscard]] std::vector<KonamiSnesInstrumentInfo> collectPercussionInfos(ByteReader reader, KonamiSnesVersion version,
                                                                           u32 tableAddress, u32 spcDirAddress) {
  std::vector<KonamiSnesInstrumentInfo> infos;
  infos.reserve(kPercussionNoteCount);
  const u32 headerSize = instrumentHeaderSize(version);
  for (u8 percussionNote = 0; percussionNote < kPercussionNoteCount; ++percussionNote) {
    const u32 address = tableAddress + headerSize * percussionNote;
    if (!reader.has(address, headerSize)) {
      break;
    }
    if (!percussionHeaderIsValid(reader, version, address, spcDirAddress)) {
      const bool legacyLayout = usesLegacyInstrumentLayout(version);
      const u8 pan = reader.u8At(address + (legacyLayout ? 6 : 5));
      if (pan > percussionPanLimit(version)) {
        break;
      }
      const int key = percussionKey(reader, address);
      if (!instrumentHeaderIsValid(reader, version, address, spcDirAddress, true) && (key < -40 || key > 40)) {
        break;
      }
      continue;
    }

    const bool legacyLayout = usesLegacyInstrumentLayout(version);
    infos.push_back(KonamiSnesInstrumentInfo{
        .index = (kDrumKitBank << 7) | kDrumKitProgram,
        .address = address,
        .srcn = reader.u8At(address),
        .key = reader.s8At(address + 1),
        .tuning = reader.s8At(address + 2),
        .adsr1 = reader.u8At(address + 3),
        .adsr2 = reader.u8At(address + 4),
        .gain = legacyLayout ? reader.u8At(address + 5) : reader.u8At(address + 4),
        .pan = reader.u8At(address + (legacyLayout ? 6 : 5)),
        .volume = reader.u8At(address + (legacyLayout ? 7 : 6)),
        .percussion = true,
        .percussionNote = percussionNote,
    });
  }
  return infos;
}

[[nodiscard]] KonamiSnesInstrumentInfo instrumentInfo(ByteReader reader, KonamiSnesVersion version, u32 index,
                                                      u32 address, bool percussion = false, u8 percussionNote = 0) {
  const bool legacyLayout = usesLegacyInstrumentLayout(version);
  return KonamiSnesInstrumentInfo{
      .index = index,
      .address = address,
      .srcn = reader.u8At(address),
      .key = reader.s8At(address + 1),
      .tuning = reader.s8At(address + 2),
      .adsr1 = reader.u8At(address + 3),
      .adsr2 = reader.u8At(address + 4),
      .gain = legacyLayout ? reader.u8At(address + 5) : reader.u8At(address + 4),
      .pan = reader.u8At(address + (legacyLayout ? 6 : 5)),
      .volume = reader.u8At(address + (legacyLayout ? 7 : 6)),
      .percussion = percussion,
      .percussionNote = percussionNote,
  };
}

[[nodiscard]] InstrumentModulation konamiInstrumentModulation(KonamiSnesVersion version) {
  const auto spec = vibrato::modulationSpec(version);
  return InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = spec.maxDepthCents,
              .rateHertz = {spec.minHertz, spec.maxHertz},
              .delaySeconds = ModulationRange{spec.minDelaySeconds, spec.maxDelaySeconds},
          },
  };
}

struct KonamiPitch {
  u8 rootKey = 72;
  s16 fineTuneCents = 0;
  Tuning aggregate;
};

[[nodiscard]] KonamiPitch konamiPitch(const KonamiSnesInstrumentInfo& info) {
  const s8 key = info.tuning >= 0 ? info.key : static_cast<s8>(info.key - 1);
  const s16 fullTuning = static_cast<s16>((static_cast<u8>(key) << 8) | static_cast<u8>(info.tuning));
  const double pitchFixer = std::log2(4286.0 / 4096.0) * 12.0;
  double coarse = 0.0;
  double fine = std::modf((fullTuning / 256.0) + pitchFixer, &coarse);
  if (fine >= 0.5) {
    coarse += 1.0;
    fine -= 1.0;
  } else if (fine <= -0.5) {
    coarse -= 1.0;
    fine += 1.0;
  }

  int root = 72 - static_cast<int>(coarse);
  if (info.percussion) {
    root += static_cast<int>(info.percussionNote) - kPercussionBaseNote;
  }
  const auto fineTuneCents = static_cast<s16>(std::lround(fine * 100.0));
  return KonamiPitch{
      .rootKey = static_cast<u8>(std::clamp(root, 0, 127)),
      .fineTuneCents = fineTuneCents,
      .aggregate = Tuning{.cents = static_cast<s32>((root - 72) * 100 + fineTuneCents)},
  };
}

[[nodiscard]] double attenuationFromVolume(u8 volume) {
  // Match legacy KonamiSnesRgn::loadRgn: the instrument byte decreases volume
  // relative to an assumed pre-pan channel level of 72 rather than acting as a
  // direct loudness value.
  const double amplitude = std::max(1.0 - (static_cast<double>(volume) / 72.0), 0.0);
  return amplitude <= 0.0 ? 100.0 : std::min(-20.0 * std::log10(amplitude), 100.0);
}

}  // namespace

std::vector<KonamiSnesInstrumentInfo> parseKonamiSnesInstrumentInfos(ByteReader reader,
                                                                     const KonamiSnesLayout& layout) {
  std::vector<KonamiSnesInstrumentInfo> infos;
  if (!layout.spcDirAddress || !layout.commonInstrumentTableAddress || !layout.bankedInstrumentTableAddress ||
      !layout.percussionInstrumentTableAddress) {
    return infos;
  }

  const u32 headerSize = instrumentHeaderSize(layout.version);
  for (u32 instrumentIndex = 0; instrumentIndex <= 0xff; ++instrumentIndex) {
    const u32 address =
        instrumentIndex < layout.firstBankedInstrument
            ? *layout.commonInstrumentTableAddress + headerSize * instrumentIndex
            : *layout.bankedInstrumentTableAddress + headerSize * (instrumentIndex - layout.firstBankedInstrument);
    if (!reader.has(address, headerSize)) {
      break;
    }
    if (!instrumentHeaderIsValid(reader, layout.version, address, *layout.spcDirAddress, false)) {
      if (instrumentIndex < layout.firstBankedInstrument) {
        continue;
      }
      break;
    }
    if (!instrumentHeaderIsValid(reader, layout.version, address, *layout.spcDirAddress, true)) {
      continue;
    }

    const u8 srcn = reader.u8At(address);
    const u32 dirEntry = *layout.spcDirAddress + srcn * 4;
    if (!reader.has(dirEntry, 4) || reader.le16(dirEntry) < dirEntry + 4) {
      continue;
    }
    infos.push_back(instrumentInfo(reader, layout.version, instrumentIndex, address));
  }

  auto percussionInfos =
      collectPercussionInfos(reader, layout.version, *layout.percussionInstrumentTableAddress, *layout.spcDirAddress);
  infos.insert(infos.end(), std::make_move_iterator(percussionInfos.begin()),
               std::make_move_iterator(percussionInfos.end()));
  return infos;
}

SnesBrrCatalog parseKonamiSnesSampleInfos(ByteReader reader, u32 spcDirAddress,
                                          const std::vector<KonamiSnesInstrumentInfo>& instruments) {
  std::vector<u8> srcns;
  srcns.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    srcns.push_back(instrument.srcn);
  }
  return readSnesBrrCatalog(reader, spcDirAddress, srcns);
}

struct KonamiSnesInstrumentBuild {
  SourceRange range;
  std::vector<Instrument> instruments;
};

KonamiSnesInstrumentBuild buildKonamiSnesInstruments(const ScanInput& input, ScanResultBuilder& builder,
                                                     ScanInstrumentSetRef instrumentSet,
                                                     ScanSampleCollectionRef sampleCollection,
                                                     KonamiSnesVersion version, u32 spcDirAddress,
                                                     const std::vector<KonamiSnesInstrumentInfo>& instrumentInfos,
                                                     const SnesBrrCatalog& samples) {
  u32 rootOffset = instrumentInfos.empty() ? 0 : instrumentInfos.front().address;
  u32 rootEnd = rootOffset;
  for (const auto& info : instrumentInfos) {
    rootOffset = std::min(rootOffset, info.address);
    rootEnd = std::max(rootEnd, info.address + instrumentHeaderSize(version));
  }
  const u32 rootSize = rootEnd >= rootOffset ? rootEnd - rootOffset : 0;
  const SourceAnnotationId root = builder.sourceMap()
                                      .table("Instrument Tables", input.reader.range(rootOffset, rootSize))
                                      .kind("konami-snes-instrument-tables")
                                      .owner(ObjectRefs::asset(instrumentSet.id))
                                      .id();

  std::map<u32, size_t> instrumentIndexByProgram;
  std::vector<Instrument> instruments;
  for (const auto& info : instrumentInfos) {
    std::optional<u32> resolvedSampleIndex;
    // Legacy regions address sample data relative to the DIR base. Preserve its
    // absolute-first lookup before falling back to this SRCN's canonical stream.
    if (const auto srcnSample = samples.index(info.srcn)) {
      const u32 sampleStart = samples.samples[*srcnSample].startAddress;
      if (sampleStart >= spcDirAddress) {
        const u32 relativeStart = sampleStart - spcDirAddress;
        resolvedSampleIndex = samples.firstIndexStartingAt(relativeStart);
        if (!resolvedSampleIndex) {
          resolvedSampleIndex = samples.canonicalIndex(info.srcn);
        }
      }
    }
    if (!resolvedSampleIndex) {
      resolvedSampleIndex = 0;
    }

    const u32 bank = info.percussion ? kDrumKitBank : (info.index >> 7);
    const u32 program = info.percussion ? kDrumKitProgram : (info.index & 0x7f);
    const u32 programKey = (bank << 7) | program;

    size_t instrumentIndex = 0;
    if (const auto found = instrumentIndexByProgram.find(programKey); found != instrumentIndexByProgram.end()) {
      instrumentIndex = found->second;
    } else {
      instrumentIndex = instruments.size();
      instrumentIndexByProgram.emplace(programKey, instrumentIndex);
      instruments.push_back(Instrument{
          .explicitAddress = InstrumentAddress{.bank = bank, .program = program},
          .name = info.percussion ? "Percussion" : fmt::format("Instrument {}", info.index),
          .range = input.reader.range(info.address, instrumentHeaderSize(version)),
          .modulation = konamiInstrumentModulation(version),
      });
    }

    auto& instrument = instruments[instrumentIndex];
    const auto pitch = konamiPitch(info);
    Region region{
        .sample = builder.sampleRef(sampleCollection, *resolvedSampleIndex),
        .range = input.reader.range(info.address, instrumentHeaderSize(version)),
        .tuning = pitch.aggregate,
        .rootKey = pitch.rootKey,
        .fineTuneCents = pitch.fineTuneCents,
        .envelope = (info.adsr1 & 0x80) != 0 ? snesDspEnvelope(info.adsr1, info.adsr2, info.gain) : Envelope{},
        .attenuationDb = attenuationFromVolume(info.volume),
    };
    if (info.percussion) {
      region.keyRange = KeyRange{.low = info.percussionNote, .high = info.percussionNote};
    }
    instrument.regions.push_back(std::move(region));

    auto annotation =
        builder.sourceMap()
            .row(info.percussion ? fmt::format("Percussion {}", static_cast<unsigned>(info.percussionNote))
                                 : fmt::format("Instrument {}", info.index),
                 input.reader.range(info.address, instrumentHeaderSize(version)))
            .role(SourceRole::Instrument)
            .kind(info.percussion ? "konami-snes-percussion-instrument" : "konami-snes-instrument")
            .owner(ObjectRefs::instrument(instrumentSet.id, info.percussion ? info.percussionNote : info.index))
            .derived("bank", bank)
            .derived("program", program)
            .field("srcn", input.reader.range(info.address, 1), info.srcn, SourceValueDisplay::Hex)
            .field("key", input.reader.range(info.address + 1, 1), info.key, SourceValueDisplay::SignedDecimal)
            .field("tuning", input.reader.range(info.address + 2, 1), info.tuning, SourceValueDisplay::SignedDecimal)
            .field("adsr1", input.reader.range(info.address + 3, 1), info.adsr1, SourceValueDisplay::Hex)
            .field("adsr2", input.reader.range(info.address + 4, 1), info.adsr2, SourceValueDisplay::Hex);
    if (usesLegacyInstrumentLayout(version)) {
      annotation.field("gain", input.reader.range(info.address + 5, 1), info.gain, SourceValueDisplay::Hex)
          .field("pan", input.reader.range(info.address + 6, 1), info.pan, SourceValueDisplay::Default)
          .field("volume", input.reader.range(info.address + 7, 1), info.volume, SourceValueDisplay::Default);
    } else {
      annotation.field("pan", input.reader.range(info.address + 5, 1), info.pan, SourceValueDisplay::Default)
          .field("volume", input.reader.range(info.address + 6, 1), info.volume, SourceValueDisplay::Default);
    }
    annotation.parent(root).link(SourceLinkRole::UsesSample,
                                 SourceTarget{ObjectRefs::sample(sampleCollection.id, *resolvedSampleIndex)});
    builder.sourceMap()
        .annotation(SourceRole::Region, "Region", input.reader.range(info.address, instrumentHeaderSize(version)))
        .kind("konami-snes-region")
        .parent(annotation.id())
        .description(fmt::format("Sample {}", *resolvedSampleIndex))
        .link(SourceLinkRole::UsesSample, SourceTarget{ObjectRefs::sample(sampleCollection.id, *resolvedSampleIndex)});
  }

  return KonamiSnesInstrumentBuild{
      .range = input.reader.range(rootOffset, rootSize),
      .instruments = std::move(instruments),
  };
}

bool addKonamiSnesSynth(const ScanInput& input, ScanResultBuilder& builder, ScanInstrumentSetRef instrumentSet,
                        ScanSampleCollectionRef sampleCollection, const KonamiSnesLayout& layout,
                        std::string_view displayName) {
  const auto instrumentInfos = parseKonamiSnesInstrumentInfos(input.reader, layout);
  const auto samples = parseKonamiSnesSampleInfos(input.reader, *layout.spcDirAddress, instrumentInfos);
  if (instrumentInfos.empty() || samples.samples.empty()) {
    return false;
  }

  auto instruments = buildKonamiSnesInstruments(input, builder, instrumentSet, sampleCollection, layout.version,
                                                *layout.spcDirAddress, instrumentInfos, samples);
  builder.instrumentSet(instrumentSet, fmt::format("{} Instruments", displayName), instruments.range)
      .instruments(std::move(instruments.instruments));
  builder.sampleCollection(sampleCollection, fmt::format("{} Samples", displayName), samples.directoryRange)
      .samples(buildSnesBrrSampleCollection(input.reader, samples, sampleCollection.id, builder.sourceMap()));
  return true;
}

}  // namespace vgmtrans::formats::konami_snes

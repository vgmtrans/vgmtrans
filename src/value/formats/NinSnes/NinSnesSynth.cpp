/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include "value/base/RecordReader.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace vgmtrans::formats::nin_snes {

using namespace core;

namespace {

constexpr double kTimerHertz = 500.0;
constexpr u8 kMelodicKeyCorrection = 24;
constexpr double kMinimumVibratoRateHertz = (kTimerHertz * 1.0) / 65536.0;
constexpr double kMaximumVibratoDelaySeconds = (256.0 * 0xff) / kTimerHertz;

struct InstrumentInfo {
  u32 program = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u8 pitchHigh = 0;
  u8 pitchLow = 0;
  SourceRange source;
  bool override = false;
};

struct InstrumentRegion {
  SampleRef sample;
  Region region;
  SourceRange source;
};

[[nodiscard]] bool blankSlot(ByteReader reader, const Profile& selected, u32 address) {
  if (selected.instruments == InstrumentLayout::Earlier5Byte) {
    return false;
  }
  const u32 size = instrumentHeaderSize(selected);
  if (!reader.has(address, size)) {
    return false;
  }
  bool allZero = true;
  bool allFf = true;
  for (u32 offset = 0; offset < size; ++offset) {
    allZero &= reader.u8At(address + offset) == 0;
    allFf &= reader.u8At(address + offset) == 0xff;
  }
  return allZero || allFf;
}

[[nodiscard]] bool validHeader(ByteReader reader, const Profile& selected, const InstrumentInfo& info,
                               u16 directoryAddress, bool inspectSample) {
  bool onlyPaddingBytes = true;
  for (u32 offset = 0; offset < instrumentHeaderSize(selected); ++offset) {
    const u8 byte = reader.u8At(static_cast<u32>(info.source.offset) + offset);
    onlyPaddingBytes &= byte == 0x00 || byte == 0xff;
  }
  if (onlyPaddingBytes) {
    return false;
  }
  if (info.srcn >= 0x80 || (info.adsr1 == 0 && info.gain == 0)) {
    return false;
  }
  const auto directory =
      readSnesSampleDirectoryEntry(reader, directoryAddress + info.srcn * 4, inspectSample);
  if (!directory || directory->startAddress > directory->loopAddress ||
      !directory->loopAddressIsBlockAligned()) {
    return false;
  }
  if (inspectSample && !directory->stream) {
    return false;
  }
  return true;
}

[[nodiscard]] InstrumentInfo readInstrument(ByteReader reader, const Profile& selected, u32 program,
                                            u32 address) {
  const bool earlier = selected.instruments == InstrumentLayout::Earlier5Byte;
  RecordReader record(reader, address, address + instrumentHeaderSize(selected));
  const u8 srcn = *record.u8("srcn", SourceValueDisplay::Hex);
  const u8 adsr1 = *record.u8("adsr1", SourceValueDisplay::Hex);
  const u8 adsr2 = *record.u8("adsr2", SourceValueDisplay::Hex);
  const u8 gain = *record.u8("gain", SourceValueDisplay::Hex);
  const u8 pitchHigh = *record.u8("pitch_high", SourceValueDisplay::Hex);
  const u8 pitchLow = earlier ? 0 : *record.u8("pitch_low", SourceValueDisplay::Hex);
  return InstrumentInfo{
      .program = program,
      .srcn = srcn,
      .adsr1 = adsr1,
      .adsr2 = adsr2,
      .gain = gain,
      .pitchHigh = pitchHigh,
      .pitchLow = pitchLow,
      .source = std::move(record).finish().range,
  };
}

[[nodiscard]] std::vector<InstrumentInfo> collectBaseInstruments(ByteReader reader,
                                                                 const Layout& layout) {
  std::vector<InstrumentInfo> infos;
  if (!layout.instrumentTableAddress || !layout.spcDirAddress) {
    return infos;
  }
  const Profile& selected = profile(layout.profile);
  const u32 size = instrumentHeaderSize(selected);
  for (u16 program = 0; program < instrumentSlotCount(selected); ++program) {
    const u32 address = *layout.instrumentTableAddress + program * size;
    if (!reader.has(address, size)) {
      break;
    }
    if (blankSlot(reader, selected, address)) {
      continue;
    }
    InstrumentInfo info = readInstrument(reader, selected, program, address);
    const u32 directoryEntry = *layout.spcDirAddress + info.srcn * 4;
    if (reader.has(directoryEntry, 4)) {
      const u16 start = reader.le16(directoryEntry);
      const u16 loop = reader.le16(directoryEntry + 2);
      if ((start == 0 && loop == 0) || (start == 0xffff && loop == 0xffff)) {
        continue;
      }
    }
    // A structurally invalid header marks the end of this table. A valid
    // header whose BRR stream is damaged is only a sparse hole.
    if (!validHeader(reader, selected, info, *layout.spcDirAddress, false)) {
      break;
    }
    if (!validHeader(reader, selected, info, *layout.spcDirAddress, true)) {
      continue;
    }
    if ((selected.base == BaseProfile::Earlier || selected.id == ProfileId::Standard) &&
        reader.le16(directoryEntry) < directoryEntry + 4) {
      continue;
    }
    infos.push_back(std::move(info));
  }
  return infos;
}

[[nodiscard]] std::vector<InstrumentInfo> collectOverrides(const SequenceRecipes& recipes) {
  std::vector<InstrumentInfo> infos;
  infos.reserve(recipes.overrides.size());
  for (const auto& definition : recipes.overrides) {
    infos.push_back(InstrumentInfo{
        .program = definition.program,
        .srcn = definition.regionData[0],
        .adsr1 = definition.regionData[1],
        .adsr2 = definition.regionData[2],
        .gain = definition.regionData[3],
        .pitchHigh = definition.regionData[4],
        .pitchLow = definition.regionData[5],
        .source = definition.source,
        .override = true,
    });
  }
  return infos;
}

[[nodiscard]] SnesBrrCatalog collectSamples(ByteReader reader, const Layout& layout,
                                            const std::vector<InstrumentInfo>& instruments) {
  std::vector<u8> srcns;
  if (profile(layout.profile).intelli == IntelliMode::Ta) {
    srcns.resize(0x80);
    for (u8 srcn = 0; srcn < 0x80; ++srcn) {
      srcns[srcn] = srcn;
    }
  } else {
    srcns.reserve(instruments.size());
    for (const auto& instrument : instruments) {
      srcns.push_back(instrument.srcn);
    }
  }
  return readSnesBrrCatalog(reader, *layout.spcDirAddress, srcns);
}

[[nodiscard]] double standardUnityKey(const Profile& selected, u8 pitchHigh, u8 pitchLow) {
  u16 pitchScale = selected.instruments == InstrumentLayout::Earlier5Byte
                       ? static_cast<u16>(static_cast<s8>(pitchHigh) * 256)
                       : static_cast<u16>((pitchHigh << 8) | pitchLow);
  if (pitchScale == 0) {
    return 96.0;
  }
  if (((static_cast<u32>(0x0217) * pitchScale) >> 8) > 0x3fff) {
    pitchScale = static_cast<u16>(
        (((static_cast<u32>(0x0217) * pitchScale) >> 8) & 0x3fff) * 256.0 / 0x0217);
  }
  double coarse = 0.0;
  double fine = std::modf(std::log2(pitchScale * (4286.0 / 4096.0) / 256.0) * 12.0,
                          &coarse);
  if (fine >= 0.5) {
    coarse += 1.0;
    fine -= 1.0;
  } else if (fine <= -0.5) {
    coarse -= 1.0;
    fine += 1.0;
  }
  const auto fineCents = static_cast<s16>(fine * 100.0);
  // VGMRgn historically stores unity key in a signed byte. Extremely low
  // pitch scales therefore overflow into its "unspecified key" representation,
  // leaving only fine tuning in the exported region. Reproduce the effective
  // tuning, not the accidental signed-byte storage detail.
  const auto legacyUnityKey =
      static_cast<s8>(96 - static_cast<int>(coarse));
  return legacyUnityKey < 0 ? 96.0 + fineCents / 100.0
                            : legacyUnityKey - fineCents / 100.0;
}

[[nodiscard]] double unityKey(ByteReader reader, const Layout& layout, const InstrumentInfo& info) {
  const Profile& selected = profile(layout.profile);
  if (selected.instruments == InstrumentLayout::KonamiTuningTable &&
      layout.konamiTuningTableAddress != 0) {
    s8 coarse = 0;
    u8 fine = 0;
    if (info.srcn < layout.konamiTuningTableSize &&
        reader.has(layout.konamiTuningTableAddress + layout.konamiTuningTableSize + info.srcn, 1)) {
      coarse = static_cast<s8>(reader.u8At(layout.konamiTuningTableAddress + info.srcn));
      fine = reader.u8At(layout.konamiTuningTableAddress + layout.konamiTuningTableSize + info.srcn);
    }
    const double correction = std::log2(4045.0 / 4096.0) * 12.0;
    const auto fineCents = static_cast<s16>(((fine / 256.0) + correction) * 100.0);
    return 71.0 - coarse - fineCents / 100.0;
  }
  return standardUnityKey(selected, info.pitchHigh, info.pitchLow);
}

[[nodiscard]] InstrumentModulation modulation(const SequenceRecipes& recipes) {
  const double maximumDepth =
      recipes.maxVibratoDepthCents > 0.0 ? recipes.maxVibratoDepthCents : 1494.140625;
  const double maximumRate =
      recipes.maxVibratoRateHertz > 0.0 ? recipes.maxVibratoRateHertz
                                        : (kTimerHertz * 0xff * 0xff) / 65536.0;
  return InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = maximumDepth,
              .rateHertz = ModulationRange{kMinimumVibratoRateHertz, maximumRate},
              .delaySeconds = ModulationRange{0.0, kMaximumVibratoDelaySeconds},
          },
  };
}

void addInstruments(InstrumentSetBuilder& builder, ByteReader reader, const Layout& layout,
                    const SequenceRecipes& recipes, const std::vector<InstrumentInfo>& infos,
                    const SnesBrrSampleRefs& samples) {
  std::map<u32, InstrumentRegion> regionsByProgram;
  const InstrumentModulation instrumentModulation = modulation(recipes);
  for (const InstrumentInfo& info : infos) {
    auto sample = samples.findSrcn(info.srcn);
    const u32 directoryEntry = *layout.spcDirAddress + info.srcn * 4;
    if (reader.has(directoryEntry, 2)) {
      const u16 start = reader.le16(directoryEntry);
      // NinSnes historically stores the BRR address relative to DIR on each
      // region, while its sample collection is not attached directly to the
      // instrument set. If that relative address itself names another BRR
      // stream, legacy exports choose the alias before falling back to SRCN.
      // A handful of Ys IV instruments make this quirk observable.
      if (start >= *layout.spcDirAddress) {
        if (const auto alias = samples.firstStartingAt(start - *layout.spcDirAddress)) {
          sample = alias;
        }
      } else {
        // The signed relative offset is negative but not the -1 sentinel.
        // Legacy therefore attempts an offset lookup, fails it, and falls
        // back to the first exported sample. FE4 places DIR near the top of
        // ARAM and makes this otherwise accidental behavior audible.
        sample = samples.atDenseIndex(0).value_or(
            SampleRef{.index = invalidIdValue});
      }
    }
    if (!sample) {
      builder.warning(fmt::format("Instrument {} sample {} was not found", info.program, info.srcn),
                      info.source);
      continue;
    }
    const std::string name =
        info.override ? fmt::format("Instrument {} (Overwrite)", info.program)
                      : fmt::format("Instrument {}", info.program);
    auto instrument =
        builder.add(info.program, Instrument{
                                      .explicitAddress =
                                          InstrumentAddress{
                                              .bank = info.program >> 7,
                                              .program = info.program & 0x7f,
                                          },
                                      .identity =
                                          InstrumentIdentity{
                                              .domain = std::string(kInstrumentDomain),
                                              .key = info.program,
                                          },
                                      .name = name,
                                      .modulation = instrumentModulation,
                                  });
    if (info.source.valid()) {
      instrument.source(name, info.source,
                        info.override ? "nin-snes-instrument-override" : "nin-snes-instrument");
    }
    Region region{
        .unityKey = unityKey(reader, layout, info),
        // The legacy NinSnes exporter never implemented direct GAIN-mode
        // envelopes. Preserve that established export contract instead of
        // inventing a decay for instrument slots whose ADSR enable bit is off.
        .envelope = (info.adsr1 & 0x80) != 0
                        ? snesDspEnvelope(info.adsr1, info.adsr2, info.gain)
                        : Envelope{},
    };
    instrument.region(*sample, region)
        .source("Region", info.source, "nin-snes-region")
        .description(fmt::format("Sample {}", sample->index));
    regionsByProgram.emplace(info.program, InstrumentRegion{
                                               .sample = *sample,
                                               .region = std::move(region),
                                               .source = info.source,
                                           });
  }

  for (const DrumKit& kit : recipes.drumKits) {
    std::vector<std::pair<const DrumSlot*, const InstrumentRegion*>> resolvedSlots;
    for (const DrumSlot& slot : kit.slots) {
      if (const auto source = regionsByProgram.find(slot.sourceProgram);
          source != regionsByProgram.end()) {
        resolvedSlots.emplace_back(&slot, &source->second);
      }
    }
    if (resolvedSlots.empty()) {
      builder.warning(fmt::format("Drum kit {} had no resolvable regions", kit.program));
      continue;
    }

    const u32 key = (0x7fu << 7) | kit.program;
    auto drum =
        builder.add(key, Instrument{
                             .explicitAddress = InstrumentAddress{.bank = 0x7f, .program = kit.program},
                             .identity =
                                 InstrumentIdentity{
                                     .domain = std::string(kInstrumentDomain),
                                     .key = key,
                                 },
                             .name = fmt::format("Drum Kit {}", kit.program),
                             .modulation = instrumentModulation,
                         });
    for (const auto& [slot, source] : resolvedSlots) {
      Region region = source->region;
      region.keyRange = KeyRange{.low = slot->key, .high = slot->key};
      if (kit.pitchModel == DrumPitchModel::IntelliPlayedNote) {
        region.unityKey +=
            static_cast<int>(slot->key) - static_cast<int>((slot->sourceNote & 0x7f) + kMelodicKeyCorrection);
      } else {
        region.unityKey += static_cast<int>(slot->key) - 0x3c - slot->globalTranspose;
      }
      drum.region(source->sample, std::move(region))
          .source(fmt::format("Drum {}", slot->key), source->source,
                  "nin-snes-drum-region");
    }
  }
}

}  // namespace

bool addSynth(ScanResultBuilder& builder, ScanInstrumentSetRef instrumentSet,
              ScanSampleCollectionRef sampleCollection, const Layout& layout,
              const SequenceRecipes& recipes, std::string_view displayName) {
  if (!layout.instrumentTableAddress || !layout.spcDirAddress) {
    return false;
  }
  const ByteReader reader = builder.reader();
  std::vector<InstrumentInfo> instruments = collectBaseInstruments(reader, layout);
  std::vector<InstrumentInfo> overrides = collectOverrides(recipes);
  instruments.insert(instruments.end(), overrides.begin(), overrides.end());
  const SnesBrrCatalog catalog = collectSamples(reader, layout, instruments);
  if (instruments.empty() || catalog.samples.empty()) {
    return false;
  }

  auto samples = builder.samples(sampleCollection);
  const SnesBrrSampleRefs sampleRefs = addSnesBrrSamples(samples, reader, catalog);
  auto instrumentBuilder = builder.instruments(instrumentSet);
  addInstruments(instrumentBuilder, reader, layout, recipes, instruments, sampleRefs);
  builder.instrumentSet(fmt::format("{} Instruments", displayName), std::move(instrumentBuilder));
  builder.sampleCollection(fmt::format("{} Samples", displayName), std::move(samples));
  return true;
}

}  // namespace vgmtrans::formats::nin_snes

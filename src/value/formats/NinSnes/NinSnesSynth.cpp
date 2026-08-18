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

struct InstrumentInfo {
  u32 program = 0;
  u8 tuningProgram = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u8 pitchHigh = 0;
  u8 pitchLow = 0;
  SourceRecord source;
  bool override = false;
  bool drumSource = false;
};

struct InstrumentRegion {
  SampleRef sample;
  Region region;
  SourceRecord source;
};

[[nodiscard]] bool blankSlot(ByteReader reader, const Profile& selected, u16 program, u32 address) {
  if (selected.instruments == InstrumentLayout::Earlier5Byte) {
    return false;
  }
  const u32 size = instrumentHeaderSize(selected);
  if (!reader.has(address, size)) {
    return false;
  }
  bool allZero = true;
  bool allFf = true;
  bool identityMappedSilent = reader.u8At(address) == static_cast<u8>(program);
  for (u32 offset = 0; offset < size; ++offset) {
    const u8 byte = reader.u8At(address + offset);
    allZero &= byte == 0;
    allFf &= byte == 0xff;
    identityMappedSilent &= offset == 0 || byte == 0;
  }
  // Some drivers initialize unused rows with only their identity-mapped SRCN.
  // Zero ADSR, GAIN, and pitch fields make the row unambiguously silent, but
  // it remains a sparse slot rather than terminating later instrument banks.
  return allZero || allFf || identityMappedSilent;
}

[[nodiscard]] bool validHeader(ByteReader reader, const Profile& selected, const InstrumentInfo& info,
                               u16 directoryAddress, bool inspectSample) {
  bool onlyPaddingBytes = true;
  for (u32 offset = 0; offset < instrumentHeaderSize(selected); ++offset) {
    const u8 byte = reader.u8At(static_cast<u32>(info.source.range.offset) + offset);
    onlyPaddingBytes &= byte == 0x00 || byte == 0xff;
  }
  if (onlyPaddingBytes) {
    return false;
  }
  if (info.srcn >= 0x80 || (info.adsr1 == 0 && info.gain == 0)) {
    return false;
  }
  const auto directory = readSnesSampleDirectoryEntry(reader, directoryAddress + info.srcn * 4, inspectSample);
  if (!directory || directory->startAddress > directory->loopAddress || !directory->loopAddressIsBlockAligned()) {
    return false;
  }
  if (inspectSample && !directory->stream) {
    return false;
  }
  return true;
}

[[nodiscard]] InstrumentInfo readInstrument(ByteReader reader, const Profile& selected, u32 program, u32 address) {
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
      .tuningProgram = static_cast<u8>(program),
      .srcn = srcn,
      .adsr1 = adsr1,
      .adsr2 = adsr2,
      .gain = gain,
      .pitchHigh = pitchHigh,
      .pitchLow = pitchLow,
      .source = std::move(record).finish(),
  };
}

[[nodiscard]] std::vector<InstrumentInfo> collectBaseInstruments(ByteReader reader, const Layout& layout) {
  std::vector<InstrumentInfo> infos;
  if (!layout.instrumentTableAddress || !layout.spcDirAddress) {
    return infos;
  }
  const Profile& selected = profile(layout.profile);
  const u32 size = instrumentHeaderSize(selected);
  u32 tableEnd = kAramSize;
  for (const u32 boundary : {layout.songListAddress, layout.playlistAddress}) {
    if (boundary > *layout.instrumentTableAddress) {
      tableEnd = std::min(tableEnd, boundary);
    }
  }
  if (layout.percussionTableAddress) {
    tableEnd = std::min(tableEnd, *layout.percussionTableAddress);
  }
  for (u16 program = 0; program < instrumentSlotCount(selected); ++program) {
    const u32 address = *layout.instrumentTableAddress + program * size;
    if (address + size > tableEnd) {
      break;
    }
    if (!reader.has(address, size)) {
      break;
    }
    if (blankSlot(reader, selected, program, address)) {
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

[[nodiscard]] std::vector<InstrumentInfo> collectEarlierPercussion(ByteReader reader, const Layout& layout,
                                                                   const SequenceRecipes& recipes) {
  std::vector<InstrumentInfo> infos;
  if (!layout.percussionTableAddress || !layout.spcDirAddress || recipes.drumKits.empty()) {
    return infos;
  }
  const Profile& selected = profile(layout.profile);
  for (const DrumSlot& slot : recipes.drumKits.front().slots) {
    if (slot.sourceProgram < kEarlierPercussionProgramBase) {
      continue;
    }
    const u32 address = *layout.percussionTableAddress + (slot.sourceProgram - kEarlierPercussionProgramBase) * 6;
    InstrumentInfo info = readInstrument(reader, selected, slot.sourceProgram, address);
    info.source.range = reader.range(address, 6);
    info.source.fields.push_back(SourceField{
        .name = "note",
        .range = reader.range(address + 5, 1),
        .value = makeSourceValue(reader.u8At(address + 5)),
        .display = SourceValueDisplay::Hex,
    });
    info.drumSource = true;
    if (validHeader(reader, selected, info, *layout.spcDirAddress, true)) {
      infos.push_back(std::move(info));
    }
  }
  return infos;
}

[[nodiscard]] std::vector<InstrumentInfo> collectOverrides(const SequenceRecipes& recipes) {
  std::vector<InstrumentInfo> infos;
  infos.reserve(recipes.overrides.size());
  for (const auto& definition : recipes.overrides) {
    infos.push_back(InstrumentInfo{
        .program = definition.program,
        .tuningProgram = definition.tuningProgram,
        .srcn = definition.srcn,
        .adsr1 = definition.adsr1,
        .adsr2 = definition.adsr2,
        .gain = definition.gain,
        .pitchHigh = definition.pitchHigh,
        .pitchLow = definition.pitchLow,
        .source = SourceRecord{.range = definition.source},
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
    pitchScale = static_cast<u16>((((static_cast<u32>(0x0217) * pitchScale) >> 8) & 0x3fff) * 256.0 / 0x0217);
  }
  double coarse = 0.0;
  double fine = std::modf(std::log2(pitchScale * (4286.0 / 4096.0) / 256.0) * 12.0, &coarse);
  if (fine >= 0.5) {
    coarse += 1.0;
    fine -= 1.0;
  } else if (fine <= -0.5) {
    coarse -= 1.0;
    fine += 1.0;
  }
  return 96.0 - coarse - fine;
}

[[nodiscard]] double konamiUnityKey(s8 coarse, u8 fine) {
  const double correction = std::log2(4045.0 / 4096.0) * 12.0;
  const auto fineCents = static_cast<s16>(((fine / 256.0) + correction) * 100.0);
  return 71.0 - coarse - fineCents / 100.0;
}

[[nodiscard]] double unityKey(ByteReader reader, const Layout& layout, const InstrumentInfo& info) {
  const Profile& selected = profile(layout.profile);
  if (selected.instruments == InstrumentLayout::KonamiTuningTable && layout.konamiTuningTableAddress != 0) {
    s8 coarse = 0;
    u8 fine = 0;
    if (info.tuningProgram < layout.konamiTuningTableSize &&
        reader.has(layout.konamiTuningTableAddress + layout.konamiTuningTableSize + info.tuningProgram, 1)) {
      coarse = static_cast<s8>(reader.u8At(layout.konamiTuningTableAddress + info.tuningProgram));
      fine = reader.u8At(layout.konamiTuningTableAddress + layout.konamiTuningTableSize + info.tuningProgram);
    }
    return konamiUnityKey(coarse, fine);
  }
  return standardUnityKey(selected, info.pitchHigh, info.pitchLow);
}

void addInstruments(InstrumentSetBuilder& builder, ByteReader reader, const Layout& layout,
                    const SequenceRecipes& recipes, const std::vector<InstrumentInfo>& infos,
                    const SnesBrrSampleRefs& samples) {
  const Profile& selected = profile(layout.profile);
  std::map<u32, InstrumentRegion> regionsByProgram;
  for (const InstrumentInfo& info : infos) {
    auto sample = samples.findSrcn(info.srcn);
    if (!sample) {
      builder.warning(fmt::format("Instrument {} sample {} was not found", info.program, info.srcn), info.source.range);
      continue;
    }
    const bool rateBasedGain = (info.adsr1 & 0x80) == 0 && (info.gain & 0x80) != 0;
    Region region{
        .unityKey = unityKey(reader, layout, info),
        // Direct GAIN is fully described by the header. Rate-based GAIN starts
        // from the DSP's live envelope, so a static region cannot infer it.
        .envelope = rateBasedGain ? Envelope{} : snesDspEnvelope(info.adsr1, info.adsr2, info.gain),
    };
    regionsByProgram.emplace(info.program, InstrumentRegion{
                                               .sample = *sample,
                                               .region = region,
                                               .source = info.source,
                                           });
    if (info.drumSource) {
      continue;
    }

    const std::string name = info.override ? fmt::format("Instrument {} (Overwrite)", info.program)
                                           : fmt::format("Instrument {}", info.program);
    auto instrument = builder.add(info.program, Instrument{
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
                                                });
    if (info.source.range.valid()) {
      instrument.source(name, info.source, info.override ? "nin-snes-instrument-override" : "nin-snes-instrument");
    }
    instrument.region(*sample, region)
        .source("Region", info.source, "nin-snes-region")
        .description(fmt::format("Sample {}", sample->index()));
  }

  for (const DrumKit& kit : recipes.drumKits) {
    std::vector<std::pair<const DrumSlot*, const InstrumentRegion*>> resolvedSlots;
    for (const DrumSlot& slot : kit.slots) {
      if (const auto source = regionsByProgram.find(slot.sourceProgram); source != regionsByProgram.end()) {
        resolvedSlots.emplace_back(&slot, &source->second);
      }
    }
    if (resolvedSlots.empty()) {
      builder.warning(fmt::format("Drum kit {} had no resolvable regions", kit.program));
      continue;
    }

    const u32 key = (0x7fu << 7) | kit.program;
    auto drum = builder.add(key, Instrument{
                                     .explicitAddress = InstrumentAddress{.bank = 0x7f, .program = kit.program},
                                     .identity =
                                         InstrumentIdentity{
                                             .domain = std::string(kInstrumentDomain),
                                             .key = key,
                                         },
                                     .name = fmt::format("Drum Kit {}", kit.program),
                                 });
    for (const auto& [slot, source] : resolvedSlots) {
      Region region = source->region;
      region.keyRange = KeyRange{.low = slot->key, .high = slot->key};
      if (selected.id == ProfileId::Konami) {
        // Konami's percussion loader explicitly clears melodic coarse/fine tuning.
        region.unityKey = konamiUnityKey(0, 0);
      }
      region.unityKey += static_cast<int>(slot->key) - slot->sourceKey;
      drum.region(source->sample, std::move(region))
          .source(fmt::format("Drum {}", slot->key), source->source, "nin-snes-drum-region");
    }
  }
}

}  // namespace

std::optional<ScanSoundBankRef> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                         const SequenceRecipes& recipes, std::string_view displayName) {
  if (!layout.instrumentTableAddress || !layout.spcDirAddress) {
    return std::nullopt;
  }
  const ByteReader reader = builder.reader();
  std::vector<InstrumentInfo> instruments = collectBaseInstruments(reader, layout);
  std::vector<InstrumentInfo> overrides = collectOverrides(recipes);
  instruments.insert(instruments.end(), overrides.begin(), overrides.end());
  std::vector<InstrumentInfo> percussion = collectEarlierPercussion(reader, layout, recipes);
  instruments.insert(instruments.end(), percussion.begin(), percussion.end());
  const SnesBrrCatalog catalog = collectSamples(reader, layout, instruments);
  if (instruments.empty() || catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instrumentDraft = builder.soundBank(fmt::format("{} Instruments", displayName));
  const SnesBrrSampleRefs sampleRefs = addSnesBrrSamples(instrumentDraft.samples(), reader, catalog);
  addInstruments(instrumentDraft.builder(), reader, layout, recipes, instruments, sampleRefs);
  return instrumentDraft.ref();
}

}  // namespace vgmtrans::formats::nin_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnes.h"

#include "value/base/RecordReader.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace vgmtrans::formats::konami_snes {

using namespace core;

namespace {

constexpr u8 kPercussionNoteCount = 0x60;
constexpr u8 kPercussionBaseNote = 0x3c;
// Konami's percussion mode selects one shared kit. Bank 127 is the conventional
// MIDI drum bank, while each source entry becomes one key region in that kit.
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
  // BRR data is stored in nine-byte blocks, so a loop point can only land on a
  // block boundary at or after the sample start.
  return sampleStart <= sampleLoop && ((sampleLoop - sampleStart) % 9) == 0;
}

// Drum tables have no explicit length. Pan and volume are checked as well as
// the sample reference so unrelated RAM is unlikely to be mistaken for entries.
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
  // Key and tuning form one signed 8.8 value. A negative fractional byte
  // borrows one from the integer byte when converted back to a whole key.
  return tuning >= 0 ? rawKey : rawKey - 1;
}

[[nodiscard]] KonamiSnesInstrumentInfo instrumentInfo(ByteReader reader, KonamiSnesVersion version, u32 index,
                                                      u32 address, bool percussion = false, u8 percussionNote = 0);

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
      // Empty or damaged drum slots may appear inside the table. Implausible
      // pan or pitch values indicate that the table has ended; otherwise skip
      // this slot and keep looking for later valid keys.
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

    infos.push_back(
        instrumentInfo(reader, version, (kDrumKitBank << 7) | kDrumKitProgram, address, true, percussionNote));
  }
  return infos;
}

[[nodiscard]] KonamiSnesInstrumentInfo instrumentInfo(ByteReader reader, KonamiSnesVersion version, u32 index,
                                                      u32 address, bool percussion, u8 percussionNote) {
  const bool legacyLayout = usesLegacyInstrumentLayout(version);
  // The early eight-byte entry stores GAIN separately. The later seven-byte entry
  // drops that byte and uses ADSR2 as the fallback GAIN value.
  RecordReader record(reader, address, address + instrumentHeaderSize(version));
  const auto srcn = record.u8("srcn", SourceValueDisplay::Hex);
  const auto key = record.s8("key");
  const auto tuning = record.s8("tuning");
  const auto adsr1 = record.u8("adsr1", SourceValueDisplay::Hex);
  const auto adsr2 = record.u8("adsr2", SourceValueDisplay::Hex);
  u8 gain = *adsr2;
  if (legacyLayout) {
    gain = *record.u8("gain", SourceValueDisplay::Hex);
  }
  const auto pan = record.u8("pan");
  const auto volume = record.u8("volume");
  return KonamiSnesInstrumentInfo{
      .index = index,
      .address = address,
      .srcn = *srcn,
      .key = *key,
      .tuning = *tuning,
      .adsr1 = *adsr1,
      .adsr2 = *adsr2,
      .gain = gain,
      .pan = *pan,
      .volume = *volume,
      .percussion = percussion,
      .percussionNote = percussionNote,
      .sourceFields = record.takeFields(),
  };
}

[[nodiscard]] InstrumentModulation konamiInstrumentModulation(KonamiSnesVersion version) {
  // Instrument exports describe the widest values the driver can produce.
  // Song-specific controller limits are added later by sequence playback.
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
  // The key byte is the whole-number part of pitch and tuning is its fractional
  // part. Join them before doing arithmetic so negative fractions keep their
  // intended value.
  const s8 key = info.tuning >= 0 ? info.key : static_cast<s8>(info.key - 1);
  const s16 fullTuning = static_cast<s16>((static_cast<u8>(key) << 8) | static_cast<u8>(info.tuning));
  // Konami tuned samples for a pitch ratio of 4286/4096 instead of an exact
  // power-of-two step. Apply that small hardware correction before splitting
  // the result into a MIDI root key and fine tuning.
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
    // Drum entries are authored relative to source note 60. Moving the region to
    // its actual drum key must move the root by the same amount.
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
    // Programs below the split come from a shared table. Programs at or above
    // it come from the bank selected when this SPC snapshot was captured.
    const u32 address =
        instrumentIndex < layout.firstBankedInstrument
            ? *layout.commonInstrumentTableAddress + headerSize * instrumentIndex
            : *layout.bankedInstrumentTableAddress + headerSize * (instrumentIndex - layout.firstBankedInstrument);
    if (!reader.has(address, headerSize)) {
      break;
    }
    if (!instrumentHeaderIsValid(reader, layout.version, address, *layout.spcDirAddress, false)) {
      // The shared table may be sparse, but the selected bank is stored as one
      // packed run. A bad shared entry is a hole; a bad banked entry ends the run.
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
    // A sample may not begin inside the directory entry that points to it. This
    // extra check filters out self-referential entries in unused RAM.
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
  // Read only samples referenced by accepted instruments. The shared sample
  // reader removes duplicate sample numbers and follows each stream to its end.
  std::vector<u8> srcns;
  srcns.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    srcns.push_back(instrument.srcn);
  }
  return readSnesBrrCatalog(reader, spcDirAddress, srcns);
}

namespace {

// Konami's legacy converter first subtracted the DIR base from a sample's
// address and tried to match that transformed address as though it were an
// absolute BRR address. Preserve that unusual lookup before the normal SRCN
// lookup because a few songs depend on the resulting sample choice.
[[nodiscard]] std::optional<SampleRef> konamiSampleRef(const KonamiSnesInstrumentInfo& info, u32 spcDirAddress,
                                                       const SnesBrrCatalog& catalog,
                                                       const SnesBrrSampleRefs& sampleRefs) {
  if (const auto catalogIndex = catalog.index(info.srcn)) {
    const u32 sampleStart = catalog.samples[*catalogIndex].startAddress;
    if (sampleStart >= spcDirAddress) {
      if (const auto transformed = sampleRefs.firstStartingAt(sampleStart - spcDirAddress)) {
        return transformed;
      }
      if (const auto direct = sampleRefs.findSrcn(info.srcn)) {
        return direct;
      }
    }
  }

  // The old path used dense sample zero when neither lookup succeeded.
  // Express that fallback through the catalog's first source entry so format
  // code still does not manufacture a dense SampleRef.
  return catalog.samples.empty() ? std::nullopt : sampleRefs.findSrcn(catalog.samples.front().srcn);
}

void addKonamiSnesInstruments(InstrumentSetBuilder& instruments, ByteReader reader, KonamiSnesVersion version,
                              u32 spcDirAddress, const std::vector<KonamiSnesInstrumentInfo>& instrumentInfos,
                              const SnesBrrCatalog& sampleCatalog, const SnesBrrSampleRefs& sampleRefs) {
  // Entries can come from three separate tables. Their common source parent
  // spans the lowest through highest entry while each exact record remains
  // separately selectable in HexView.
  u32 rootOffset = instrumentInfos.front().address;
  u32 rootEnd = rootOffset + instrumentHeaderSize(version);
  for (const auto& info : instrumentInfos) {
    rootOffset = std::min(rootOffset, info.address);
    rootEnd = std::max(rootEnd, info.address + instrumentHeaderSize(version));
  }
  const SourceRange tableRange = reader.range(rootOffset, rootEnd - rootOffset);
  instruments.include(tableRange);
  const SourceAnnotationId root =
      instruments.source(SourceRole::Table, "Instrument Tables", tableRange, "konami-snes-instrument-tables").id();

  for (const auto& info : instrumentInfos) {
    const auto sample = konamiSampleRef(info, spcDirAddress, sampleCatalog, sampleRefs);
    if (!sample) {
      instruments.warning("Instrument sample was not found", reader.range(info.address, instrumentHeaderSize(version)));
      continue;
    }

    const u32 bank = info.percussion ? kDrumKitBank : (info.index >> 7);
    const u32 program = info.percussion ? kDrumKitProgram : (info.index & 0x7f);
    const u32 programKey = (bank << 7) | program;
    const SourceRange entryRange = reader.range(info.address, instrumentHeaderSize(version));
    const std::string entryName = info.percussion
                                      ? fmt::format("Percussion {}", static_cast<unsigned>(info.percussionNote))
                                      : fmt::format("Instrument {}", info.index);
    auto instrument =
        instruments.getOrAdd(programKey, Instrument{
                                             .explicitAddress = InstrumentAddress{.bank = bank, .program = program},
                                             .name = info.percussion ? "Percussion" : entryName,
                                             .modulation = konamiInstrumentModulation(version),
                                         });
    instrument
        .source(entryName, entryRange, info.percussion ? "konami-snes-percussion-instrument" : "konami-snes-instrument")
        .derived("bank", bank)
        .derived("program", program)
        .fields(info.sourceFields)
        .parent(root);

    const auto pitch = konamiPitch(info);
    Region region{
        .tuning = pitch.aggregate,
        .rootKey = pitch.rootKey,
        .fineTuneCents = pitch.fineTuneCents,
        // ADSR1 bit 7 chooses the DSP's ADSR envelope. When clear, the driver
        // uses GAIN behavior that cannot be represented as the same envelope.
        .envelope = (info.adsr1 & 0x80) != 0 ? snesDspEnvelope(info.adsr1, info.adsr2, info.gain) : Envelope{},
        .attenuationDb = attenuationFromVolume(info.volume),
    };
    if (info.percussion) {
      region.keyRange = KeyRange{.low = info.percussionNote, .high = info.percussionNote};
    }
    instrument.region(*sample, std::move(region))
        .source("Region", entryRange, "konami-snes-region")
        .description(fmt::format("Sample {}", sample->index));
  }
}

}  // namespace

bool addKonamiSnesSynth(ScanResultBuilder& builder, ScanInstrumentSetRef instrumentSet,
                        ScanSampleCollectionRef sampleCollection, const KonamiSnesLayout& layout,
                        std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const auto instrumentInfos = parseKonamiSnesInstrumentInfos(reader, layout);
  const auto sampleCatalog = parseKonamiSnesSampleInfos(reader, *layout.spcDirAddress, instrumentInfos);
  // Do not publish half of a synth. An instrument set without sample data (or
  // vice versa) cannot produce a usable export.
  if (instrumentInfos.empty() || sampleCatalog.samples.empty()) {
    return false;
  }

  auto samples = builder.samples(sampleCollection);
  const auto sampleRefs = addSnesBrrSamples(samples, reader, sampleCatalog);

  auto instruments = builder.instruments(instrumentSet);
  addKonamiSnesInstruments(instruments, reader, layout.version, *layout.spcDirAddress, instrumentInfos, sampleCatalog,
                           sampleRefs);

  builder.instrumentSet(fmt::format("{} Instruments", displayName), std::move(instruments));
  builder.sampleCollection(fmt::format("{} Samples", displayName), std::move(samples));
  return true;
}

}  // namespace vgmtrans::formats::konami_snes

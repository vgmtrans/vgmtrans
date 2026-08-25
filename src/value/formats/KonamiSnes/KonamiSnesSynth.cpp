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
constexpr u32 kInstrumentProgramCount = 0x100;
// Konami's percussion mode selects one shared kit. Bank 127 is the conventional
// MIDI drum bank, while each source entry becomes one key region in that kit.
constexpr u32 kDrumKitBank = 0x7f;
constexpr u32 kDrumKitProgram = 0x00;

[[nodiscard]] KonamiSnesInstrumentInfo parseInstrumentInfo(ByteReader reader, KonamiSnesVersion version, u32 index,
                                                           u32 address, bool percussion = false,
                                                           u8 percussionNote = 0) {
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
      .source = std::move(record).finish(),
  };
}

[[nodiscard]] bool instrumentHeaderIsValid(ByteReader reader, const KonamiSnesInstrumentInfo& info, u32 spcDirAddress,
                                           bool validateSample) {
  if (info.srcn == 0xff) {
    return false;
  }

  return readSnesSampleDirectoryEntry(reader, spcDirAddress + info.srcn * 4, validateSample).has_value();
}

[[nodiscard]] bool sampleStartsAfterDirectoryEntry(ByteReader reader, u32 spcDirAddress, u8 srcn) {
  const u32 dirEntry = spcDirAddress + static_cast<u32>(srcn) * 4;
  return reader.has(dirEntry, 4) && reader.le16(dirEntry) >= dirEntry + 4;
}

[[nodiscard]] int percussionKey(const KonamiSnesInstrumentInfo& info) {
  // Key and tuning form one signed 8.8 value. A negative fractional byte
  // borrows one from the integer byte when converted back to a whole key.
  return info.tuning >= 0 ? info.key : info.key - 1;
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
    auto info =
        parseInstrumentInfo(reader, version, (kDrumKitBank << 7) | kDrumKitProgram, address, true, percussionNote);
    const bool sampleIsValid = instrumentHeaderIsValid(reader, info, spcDirAddress, true);
    // Drum tables have no explicit length. Pan and volume are checked as well
    // as the sample reference so unrelated RAM is unlikely to look like data.
    if (!sampleIsValid || info.pan > instrumentPanLimit(version) || info.volume > 0x7f) {
      // Empty or damaged drum slots may appear inside the table. Implausible
      // pan or pitch values indicate that the table has ended; otherwise skip
      // this slot and keep looking for later valid keys.
      if (info.pan > instrumentPanLimit(version)) {
        break;
      }
      const int key = percussionKey(info);
      if (!sampleIsValid && (key < -40 || key > 40)) {
        break;
      }
      continue;
    }

    infos.push_back(std::move(info));
  }
  return infos;
}

enum class InstrumentRangeLayout {
  Sparse,
  Packed,
};

void appendMelodicInstrumentRange(ByteReader reader, KonamiSnesVersion version, u32 spcDirAddress, u32 firstProgram,
                                  u32 endProgram, u32 tableAddress, u32 tableFirstProgram,
                                  InstrumentRangeLayout rangeLayout, std::vector<KonamiSnesInstrumentInfo>& infos) {
  const u32 headerSize = instrumentHeaderSize(version);
  for (u32 program = firstProgram; program < endProgram; ++program) {
    const u32 address = tableAddress + headerSize * (program - tableFirstProgram);
    if (!reader.has(address, headerSize)) {
      break;
    }

    auto info = parseInstrumentInfo(reader, version, program, address);
    // Reject false-positive melodic entries whose sample begins before its DIR
    // entry. Do not apply this heuristic to percussion; valid percussion
    // samples may precede the DIR table.
    const bool structurallyValid = info.pan <= instrumentPanLimit(version) &&
                                   instrumentHeaderIsValid(reader, info, spcDirAddress, false) &&
                                   sampleStartsAfterDirectoryEntry(reader, spcDirAddress, info.srcn);
    if (!structurallyValid) {
      if (rangeLayout == InstrumentRangeLayout::Packed) {
        break;
      }
      continue;
    }
    if (!instrumentHeaderIsValid(reader, info, spcDirAddress, true)) {
      continue;
    }
    infos.push_back(std::move(info));
  }
}

[[nodiscard]] double konamiUnityKey(const KonamiSnesInstrumentInfo& info) {
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
  return std::clamp(root, 0, 127) - (fineTuneCents / 100.0);
}

}  // namespace

std::vector<KonamiSnesInstrumentInfo> parseKonamiSnesInstrumentInfos(ByteReader reader,
                                                                     const KonamiSnesLayout& layout) {
  std::vector<KonamiSnesInstrumentInfo> infos;
  if (!layout.spcDirAddress || !layout.commonInstrumentTableAddress || !layout.bankedInstrumentTableAddress ||
      !layout.percussionInstrumentTableAddress) {
    return infos;
  }

  const u32 firstBankedProgram = layout.firstBankedInstrument;
  const u32 bankedEnd = std::clamp<u32>(layout.bankedInstrumentEnd, firstBankedProgram, kInstrumentProgramCount);

  // Programs before the banked range may have unused entries. The banked range
  // and any common-table suffix are contiguous, so their first invalid entry
  // ends the range.
  appendMelodicInstrumentRange(reader, layout.version, *layout.spcDirAddress, 0, firstBankedProgram,
                               *layout.commonInstrumentTableAddress, 0, InstrumentRangeLayout::Sparse, infos);
  appendMelodicInstrumentRange(reader, layout.version, *layout.spcDirAddress, firstBankedProgram, bankedEnd,
                               *layout.bankedInstrumentTableAddress, firstBankedProgram, InstrumentRangeLayout::Packed,
                               infos);
  appendMelodicInstrumentRange(reader, layout.version, *layout.spcDirAddress, bankedEnd, kInstrumentProgramCount,
                               *layout.commonInstrumentTableAddress, 0, InstrumentRangeLayout::Packed, infos);

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

void addKonamiSnesInstruments(InstrumentSetBuilder& instruments, ByteReader reader,
                              const std::vector<KonamiSnesInstrumentInfo>& instrumentInfos,
                              const SnesBrrSampleRefs& sampleRefs) {
  // Entries can come from three separate tables. Their common source parent
  // spans the lowest through highest entry while each exact record remains
  // separately selectable in HexView.
  u32 rootOffset = static_cast<u32>(instrumentInfos.front().source.range.offset);
  u32 rootEnd = static_cast<u32>(instrumentInfos.front().source.range.endOffset());
  for (const auto& info : instrumentInfos) {
    rootOffset = std::min(rootOffset, static_cast<u32>(info.source.range.offset));
    rootEnd = std::max(rootEnd, static_cast<u32>(info.source.range.endOffset()));
  }
  const SourceRange tableRange = reader.range(rootOffset, rootEnd - rootOffset);
  instruments.include(tableRange);
  const SourceAnnotationId root =
      instruments.source(SourceRole::Table, "Instrument Tables", tableRange, "konami-snes-instrument-tables").id();

  for (const auto& info : instrumentInfos) {
    const auto sample = sampleRefs.findSrcn(info.srcn);
    if (!sample) {
      instruments.warning("Instrument sample was not found", info.source.range);
      continue;
    }

    const u32 bank = info.percussion ? kDrumKitBank : (info.index >> 7);
    const u32 program = info.percussion ? kDrumKitProgram : (info.index & 0x7f);
    const u32 programKey = (bank << 7) | program;
    const SourceRange entryRange = info.source.range;
    const std::string entryName = info.percussion
                                      ? fmt::format("Percussion {}", static_cast<unsigned>(info.percussionNote))
                                      : fmt::format("Instrument {}", info.index);
    auto instrument =
        instruments.getOrAdd(programKey, Instrument{
                                             .explicitAddress = InstrumentAddress{.bank = bank, .program = program},
                                             .name = info.percussion ? "Percussion" : entryName,
                                         });
    instrument
        .source(entryName, info.source,
                info.percussion ? "konami-snes-percussion-instrument" : "konami-snes-instrument")
        .parent(root);

    const double unityKey = konamiUnityKey(info);
    Region region{
        .unityKey = unityKey,
        // The shared DSP conversion understands both ADSR and GAIN modes.
        .envelope = snesDspEnvelope(info.adsr1, info.adsr2, info.gain),
    };
    if (info.percussion) {
      region.keyRange = KeyRange{.low = info.percussionNote, .high = info.percussionNote};
    }
    instrument.region(*sample, std::move(region))
        .source("Region", entryRange, "konami-snes-region")
        .description(fmt::format("Sample {}", sample->index()));
  }
}

}  // namespace

std::optional<ScanSoundBankDraft> addKonamiSnesSynth(ScanResultBuilder& builder, const KonamiSnesLayout& layout,
                                                     const std::vector<KonamiSnesInstrumentInfo>& instrumentInfos,
                                                     std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const auto sampleCatalog = parseKonamiSnesSampleInfos(reader, *layout.spcDirAddress, instrumentInfos);
  // Do not publish half of a synth. A sound bank without sample data (or
  // vice versa) cannot produce a usable export.
  if (instrumentInfos.empty() || sampleCatalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.soundBank(fmt::format("{} Instruments", displayName));
  const auto sampleRefs = addSnesBrrSamples(instruments.samples(), reader, sampleCatalog);

  addKonamiSnesInstruments(instruments.builder(), reader, instrumentInfos, sampleRefs);

  return instruments;
}

}  // namespace vgmtrans::formats::konami_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"

#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <fmt/format.h>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

[[nodiscard]] Envelope akaoRegionEnvelope(const AkaoArticulation& articulation, u8 attackRate, u8 sustainRate,
                                          u8 sustainMode, u8 releaseRate) {
  u16 adsr1 = articulation.adsr1;
  u16 adsr2 = articulation.adsr2;
  // Legacy Akao applies region-level ADSR bytes over the articulation ADSR. Keep that
  // behavior here so key-split and drum regions retain their per-region shaping.
  adsr1 &= static_cast<u16>(~0x7f00u);
  adsr1 |= static_cast<u16>((attackRate & 0x7f) << 8);
  adsr2 &= static_cast<u16>(~0xffdfu);
  adsr2 |= static_cast<u16>((sustainRate & 0x7f) << 6);
  adsr2 |= static_cast<u16>((sustainMode & 0x07) << 13);
  adsr2 |= static_cast<u16>(releaseRate & 0x1f);
  return psxSpuEnvelope(adsr1, adsr2);
}

void applyArticulationToRegion(Region& region, const AkaoArticulationBinding* binding, u8 attackRate, u8 sustainRate,
                               u8 sustainMode, u8 releaseRate, bool drum, u8 drumRelativeUnityKey = 0) {
  if (binding == nullptr) {
    return;
  }
  const AkaoArticulation& articulation = binding->articulation;
  region.sample = SampleRef{.collection = binding->collection.id, .index = binding->sampleIndex};
  const double rootKey =
      drum ? articulation.unityKey + region.keyRange.low - drumRelativeUnityKey : articulation.unityKey;
  region.unityKey = rootKey - (articulation.fineTuneCents / 100.0);
  region.envelope = akaoRegionEnvelope(articulation, attackRate, sustainRate, sustainMode, releaseRate);
  region.loop = articulation.loop;
}

[[nodiscard]] const AkaoArticulationBinding* findArticulation(const AkaoArticulationMap& articulations,
                                                              u32 articulationId) {
  const auto found = articulations.find(articulationId);
  return found == articulations.end() ? nullptr : &found->second;
}

void requireArticulation(std::set<u32>& required, u32 articulationId) {
  if (articulationId != 0) {
    required.insert(articulationId);
  }
}

[[nodiscard]] std::vector<Region> readMelodicRegions(ByteReader reader, u32 offset, u32 endOffset,
                                                     const AkaoProfile& profile,
                                                     const AkaoArticulationMap& articulations,
                                                     std::set<u32>& required) {
  std::vector<Region> regions;
  for (u32 i = 0; i < 128 && offset + i * 8 + 8 <= endOffset && reader.has(offset + i * 8, 8); ++i) {
    const u32 regionOffset = offset + i * 8;
    if (!profile.version3OrLater() && reader.u8At(regionOffset) >= 0x80) {
      break;
    }
    if (profile.version3OrLater() && reader.le32(regionOffset) == 0) {
      break;
    }

    const u8 articulationId = reader.u8At(regionOffset);
    requireArticulation(required, articulationId);
    Region region{
        .keyRange = KeyRange{.low = reader.u8At(regionOffset + 1), .high = reader.u8At(regionOffset + 2)},
        .velocityRange = VelocityRange{.low = 0, .high = 127},
        .sample = SampleRef{.index = invalidIdValue},
        .range = reader.range(regionOffset, 8),
        .attenuationDb = linearAmplitudeToAttenuationDb(
            reader.u8At(regionOffset + 7) == 0 ? 1.0 : reader.u8At(regionOffset + 7) / 128.0),
    };
    applyArticulationToRegion(region, findArticulation(articulations, articulationId), reader.u8At(regionOffset + 3),
                              reader.u8At(regionOffset + 4), reader.u8At(regionOffset + 5),
                              reader.u8At(regionOffset + 6), false);
    if (!regions.empty()) {
      Region& previous = regions.back();
      // Some tables contain stale or overlapping entries. The driver advances
      // only when the next entry extends the covered key range, and fills any
      // gap from the preceding entry's end.
      if (region.keyRange.high > previous.keyRange.high && region.keyRange.low > previous.keyRange.high) {
        if (region.keyRange.low > previous.keyRange.high + 1) {
          region.keyRange.low = static_cast<u8>(previous.keyRange.high + 1);
        }
        regions.push_back(region);
      }
    } else {
      regions.push_back(region);
    }
  }
  if (!regions.empty()) {
    regions.front().keyRange.low = 0;
    regions.back().keyRange.high = 127;
  }
  return regions;
}

void addMelodicInstrument(std::vector<Instrument>& instruments, ByteReader reader, u32 offset, u32 endOffset,
                          const AkaoProfile& profile, const AkaoArticulationMap& articulations, std::set<u32>& required,
                          u32 program) {
  auto regions = readMelodicRegions(reader, offset, endOffset, profile, articulations, required);
  if (regions.empty()) {
    return;
  }
  const auto& last = regions.back();
  instruments.push_back(Instrument{
      .explicitAddress = InstrumentAddress{.bank = 1, .program = program},
      .name = fmt::format("Instrument {}", program),
      .range = reader.range(offset, static_cast<u32>(last.range.offset + last.range.size - offset)),
      .regions = std::move(regions),
  });
}

void addDrumInstrument(std::vector<Instrument>& instruments, ByteReader reader, u32 offset, u32 endOffset,
                       const AkaoProfile& profile, const AkaoArticulationMap& articulations, std::set<u32>& required,
                       u32 program = 127) {
  Instrument drum{
      .explicitAddress = InstrumentAddress{.bank = 127, .program = program},
      .name = "Drum Kit",
      .range = reader.range(offset, 0),
  };
  if (profile.version3OrLater()) {
    const u8 attackRate = reader.u8At(offset + 2);
    const u8 sustainRate = reader.u8At(offset + 3);
    const u8 sustainMode = reader.u8At(offset + 4);
    const u8 releaseRate = reader.u8At(offset + 5);
    for (u32 key = 0; key < 128; ++key) {
      const u32 regionOffset = offset + key * 8;
      if (regionOffset + 8 > endOffset || !reader.has(regionOffset, 8)) {
        break;
      }
      if (reader.le32(regionOffset) == 0 && reader.le32(regionOffset + 4) == 0) {
        continue;
      }
      if (reader.le32(regionOffset) == 0xffffffff && reader.le32(regionOffset + 4) == 0xffffffff) {
        break;
      }
      const u8 articulationId = reader.u8At(regionOffset);
      requireArticulation(required, articulationId);
      Region region{
          .keyRange = KeyRange{.low = static_cast<u8>(key), .high = static_cast<u8>(key)},
          .velocityRange = VelocityRange{.low = 0, .high = 127},
          .sample = SampleRef{.index = invalidIdValue},
          .range = reader.range(regionOffset, 8),
          .pan = panPositionFrom7Bit(reader.u8At(regionOffset + 7) & 0x7f),
          .attenuationDb = linearAmplitudeToAttenuationDb(
              reader.u8At(regionOffset + 6) == 0 ? 1.0 : reader.u8At(regionOffset + 6) / 128.0),
      };
      applyArticulationToRegion(region, findArticulation(articulations, articulationId), attackRate, sustainRate,
                                sustainMode, releaseRate, true, reader.u8At(regionOffset + 1));
      drum.regions.push_back(region);
    }
  } else {
    const u32 regionSize = profile.legacyDrumRegionBytes();
    for (u32 drumKey = 0; drumKey < 12; ++drumKey) {
      const u32 regionOffset = offset + drumKey * regionSize;
      if (regionOffset + regionSize > endOffset || !reader.has(regionOffset, regionSize)) {
        break;
      }
      if (profile.legacyDrumRegionIsBlank(reader, regionOffset)) {
        continue;
      }
      const u8 articulationId = reader.u8At(regionOffset);
      const u8 key = static_cast<u8>(24 + drumKey);
      requireArticulation(required, articulationId);
      Region region{
          .keyRange = KeyRange{.low = key, .high = key},
          .velocityRange = VelocityRange{.low = 0, .high = 127},
          .sample = SampleRef{.index = invalidIdValue},
          .range = reader.range(regionOffset, regionSize),
          .pan = panPositionFrom7Bit(reader.u8At(regionOffset + 4)),
          .attenuationDb = linearAmplitudeToAttenuationDb(reader.le16(regionOffset + 2) / (127.0 * 128.0)),
      };
      applyArticulationToRegion(region, findArticulation(articulations, articulationId), 0, 0, 0, 0, true,
                                reader.u8At(regionOffset + 1));
      drum.regions.push_back(region);
    }
  }
  if (!drum.regions.empty()) {
    const auto& last = drum.regions.back();
    drum.range.size = static_cast<u32>(last.range.offset + last.range.size - offset);
    instruments.push_back(std::move(drum));
  }
}

void addSyntheticArticulationInstruments(std::vector<Instrument>& instruments,
                                         const AkaoArticulationMap& articulations) {
  for (const auto& [articulationId, binding] : articulations) {
    Region region{
        .keyRange = KeyRange{.low = 0, .high = 127},
        .velocityRange = VelocityRange{.low = 0, .high = 127},
        .sample = SampleRef{.collection = binding.collection.id, .index = binding.sampleIndex},
        .range = binding.articulation.source.range,
        .unityKey = binding.articulation.unityKey - (binding.articulation.fineTuneCents / 100.0),
        .envelope = psxSpuEnvelope(binding.articulation.adsr1, binding.articulation.adsr2),
        .loop = binding.articulation.loop,
    };
    instruments.push_back(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = articulationId},
        .name = fmt::format("Articulation {}", articulationId),
        .range = binding.articulation.source.range,
        .regions = {region},
    });
  }
}

struct ParsedInstrumentSet {
  std::vector<Instrument> instruments;
  std::vector<u32> requiredArticulations;
};

[[nodiscard]] ParsedInstrumentSet parseInstrumentTables(ByteReader reader, const AkaoSequenceAnalysis& sequence,
                                                        const AkaoArticulationMap& articulations) {
  ParsedInstrumentSet parsed;
  std::set<u32> required;
  const AkaoProfile profile{.version = sequence.header.version};
  const u32 sequenceEnd = sequence.header.offset + sequence.header.length;

  if (sequence.header.instrumentSetOffset) {
    const u32 instrSetOffset = *sequence.header.instrumentSetOffset;
    for (u32 program = 0; program < 16; ++program) {
      const u64 pointerOffset = static_cast<u64>(instrSetOffset) + program * 2;
      if (pointerOffset + 2 > sequenceEnd || !reader.has(pointerOffset, 2)) {
        break;
      }
      const u16 pointer = reader.le16(pointerOffset);
      if (pointer == 0xffff || (pointer == 0 && program != 0)) {
        continue;
      }
      const u64 instrOffset = static_cast<u64>(instrSetOffset) + 0x20 + pointer;
      if (instrOffset < sequenceEnd && reader.has(instrOffset, 8)) {
        addMelodicInstrument(parsed.instruments, reader, static_cast<u32>(instrOffset), sequenceEnd, profile,
                             articulations, required, program);
      }
    }
  } else {
    u32 program = 0;
    for (const u32 instrOffset : sequence.references.customInstrumentTableOffsets) {
      if (instrOffset < sequenceEnd && reader.has(instrOffset, 8)) {
        addMelodicInstrument(parsed.instruments, reader, instrOffset, sequenceEnd, profile, articulations, required,
                             program++);
      }
    }
  }

  if (sequence.header.drumSetOffset && *sequence.header.drumSetOffset < sequenceEnd) {
    addDrumInstrument(parsed.instruments, reader, *sequence.header.drumSetOffset, sequenceEnd, profile, articulations,
                      required);
  } else {
    u32 drumIndex = 0;
    for (const u32 drumOffset : sequence.references.drumInstrumentTableOffsets) {
      if (drumOffset < sequenceEnd && reader.has(drumOffset, 5)) {
        addDrumInstrument(parsed.instruments, reader, drumOffset, sequenceEnd, profile, articulations, required,
                          127 - drumIndex++);
      }
    }
  }

  if (!sequence.header.sampleSetId || *sequence.header.sampleSetId == 0) {
    required.insert(sequence.references.individualArticulationIds.begin(),
                    sequence.references.individualArticulationIds.end());
  }
  parsed.requiredArticulations.assign(required.begin(), required.end());
  return parsed;
}

void includeSpan(std::optional<SourceRange>& span, SourceRange range) {
  if (!range.valid()) {
    return;
  }
  if (!span) {
    span = range;
    return;
  }
  if (span->source != range.source) {
    return;
  }
  const u64 start = std::min(span->offset, range.offset);
  const u64 end = std::max(span->endOffset(), range.endOffset());
  span->offset = start;
  span->size = end - start;
}

[[nodiscard]] std::optional<SourceRange> instrumentSpan(const std::vector<Instrument>& instruments) {
  std::optional<SourceRange> span;
  for (const Instrument& instrument : instruments) {
    includeSpan(span, instrument.range);
    for (const Region& region : instrument.regions) {
      includeSpan(span, region.range);
    }
  }
  return span;
}

[[nodiscard]] SourceRange instrumentSetRange(ByteReader reader, const AkaoSequenceAnalysis& sequence,
                                             const std::vector<Instrument>& instruments) {
  std::optional<SourceRange> span = instrumentSpan(instruments);
  const u64 sequenceEnd = static_cast<u64>(sequence.header.offset) + sequence.header.length;
  const auto includeAnchor = [&](u32 offset) {
    if (offset < sequenceEnd && reader.has(offset, 0)) {
      includeSpan(span, reader.range(offset, 0));
    }
  };

  if (sequence.header.instrumentSetOffset) {
    const u32 offset = *sequence.header.instrumentSetOffset;
    if (offset < sequenceEnd && reader.has(offset, 0)) {
      includeSpan(span, reader.range(offset, std::min<u64>(0x20, sequenceEnd - offset)));
    }
  }
  if (sequence.header.drumSetOffset) {
    includeAnchor(*sequence.header.drumSetOffset);
  }
  for (const u32 offset : sequence.references.customInstrumentTableOffsets) {
    includeAnchor(offset);
  }
  for (const u32 offset : sequence.references.drumInstrumentTableOffsets) {
    includeAnchor(offset);
  }

  // Individual-articulation sequences have no local instrument table. They
  // still need a durable, inspectable bank asset, anchored to the sequence that
  // defines its programs.
  return span.value_or(reader.range(sequence.header.offset, 0));
}

[[nodiscard]] std::string_view instrumentKind(const InstrumentAddress& address) {
  std::string_view kind = "akao-instrument";
  if (address.bank == 0) {
    kind = "akao-articulation-instrument";
  } else if (address.bank == 127) {
    kind = "akao-drum-kit";
  }
  return kind;
}

void annotateArticulation(ByteReader reader, AnnotationBuilder& annotation, const Region& region,
                          std::optional<u32> derivedArticulationId = std::nullopt) {
  if (derivedArticulationId) {
    annotation.derived("articulation_id", *derivedArticulationId);
  } else if (region.range.valid() && reader.has(region.range.offset, 1)) {
    annotation.field("articulation_id", reader.range(region.range.offset, 1), reader.u8At(region.range.offset),
                     SourceValueDisplay::Hex);
  }
}

void annotateInstrumentPointerTable(ByteReader reader, const AkaoSequenceAnalysis& sequence,
                                    InstrumentSetBuilder& instruments, SourceAnnotationId parent) {
  if (!sequence.header.instrumentSetOffset) {
    return;
  }
  const u32 tableOffset = *sequence.header.instrumentSetOffset;
  const u64 sequenceEnd = static_cast<u64>(sequence.header.offset) + sequence.header.length;
  if (tableOffset >= sequenceEnd || !reader.has(tableOffset, 2)) {
    return;
  }

  const u32 tableSize = static_cast<u32>(std::min<u64>(0x20, sequenceEnd - tableOffset));
  auto table = instruments
                   .source(SourceRole::Table, "Instrument Pointer Table", reader.range(tableOffset, tableSize),
                           "akao-instrument-pointer-table")
                   .parent(parent);
  for (u32 program = 0; program < 16 && program * 2 + 2 <= tableSize; ++program) {
    const u32 pointerOffset = tableOffset + program * 2;
    const u16 pointer = reader.le16(pointerOffset);
    auto entry = instruments
                     .source(SourceRole::TableEntry, fmt::format("Instrument {} Pointer", program),
                             reader.range(pointerOffset, 2), "akao-instrument-pointer")
                     .parent(table.id())
                     .derived("program", program)
                     .field("relative", reader.range(pointerOffset, 2), pointer, SourceValueDisplay::Hex);
    if (pointer == 0xffff || (pointer == 0 && program != 0)) {
      continue;
    }
    const u64 target = static_cast<u64>(tableOffset) + 0x20 + pointer;
    if (target < sequenceEnd && reader.has(target, 1)) {
      entry.link(SourceLinkRole::PointsTo, reader.range(target, 0), "Instrument");
    }
  }
}

void publishInstrument(ByteReader reader, InstrumentSetBuilder& out, SourceAnnotationId parent, Instrument instrument) {
  const InstrumentAddress address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
  const std::string name = instrument.name;
  const SourceRange range = instrument.range;
  auto entry = out.append(std::move(instrument));
  entry.source(name, range, instrumentKind(address)).parent(parent);

  for (u32 i = 0; i < entry.value().regions.size(); ++i) {
    const Region& region = entry.value().regions[i];
    auto regionAnnotation = entry.regionAt(i).source("Region", region.range, "akao-region");
    annotateArticulation(reader, regionAnnotation, region,
                         address.bank == 0 ? std::optional{address.program} : std::nullopt);
  }
}

}  // namespace

std::string akaoInstrumentSetName(const AkaoSequenceAnalysis& sequence) {
  return fmt::format("Akao Instr Set {:02X}", sequence.header.sequenceId);
}

std::vector<u32> buildAkaoInstrumentSet(const ScanInput& input, const AkaoSequenceAnalysis& sequence,
                                        const AkaoArticulationMap& articulations, InstrumentSetBuilder& instruments) {
  auto parsed = parseInstrumentTables(input.reader, sequence, articulations);
  const SourceRange range = instrumentSetRange(input.reader, sequence, parsed.instruments);
  if (sequence.references.usesIndividualArticulations) {
    addSyntheticArticulationInstruments(parsed.instruments, articulations);
  }

  instruments.include(range);
  auto root =
      instruments.source(SourceRole::InstrumentSet, akaoInstrumentSetName(sequence), range, "akao-instrument-set")
          .derived("instrument_count", parsed.instruments.size())
          .derived("required_articulation_count", parsed.requiredArticulations.size())
          .derived("uses_individual_articulations", sequence.references.usesIndividualArticulations,
                   SourceValueDisplay::Boolean);
  annotateInstrumentPointerTable(input.reader, sequence, instruments, root.id());

  for (auto& instrument : parsed.instruments) {
    publishInstrument(input.reader, instruments, root.id(), std::move(instrument));
  }
  return parsed.requiredArticulations;
}

}  // namespace vgmtrans::formats::akao

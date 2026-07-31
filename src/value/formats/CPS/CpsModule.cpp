/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CPS/Cps.h"

#include "value/extractors/MameRomSetExtractor.h"

#include <fmt/format.h>

#include <optional>
#include <vector>

namespace vgmtrans::formats::cps {

using namespace core;

namespace {

[[nodiscard]] CollectionKey collectionKey(SourceId source, u32 sequenceIndex) {
  return CollectionKey{
      .resolver = std::string(kCpsFormatName),
      .value = "source:" + std::to_string(source.value) + ":sequence:" + std::to_string(sequenceIndex),
  };
}

void annotateSequenceTable(SourceMapBuilder& sourceMap, ByteReader reader, const CpsLayout& layout,
                           SourceAnnotationId table) {
  const u64 entrySize = isCps1(layout.version) ? 2 : 4;
  for (u64 relative = 0, index = 0; relative < layout.sequenceTableLength; relative += entrySize, ++index) {
    const u64 entryOffset = layout.sequenceTableOffset + relative;
    const SourceRange entryRange = reader.range(entryOffset, entrySize);
    const u32 encoded =
        entrySize == 4 ? reader.be32(entryOffset)
                       : (layout.version == CpsVersion::Cps1V100 ? reader.le16(entryOffset) : reader.be16(entryOffset));
    if (encoded == 0) {
      continue;
    }

    const auto target = cpsSequenceAddress(layout, encoded);
    const std::string label = fmt::format("Sequence Pointer {}", index);
    auto annotation = target && reader.has(*target, 1)
                          ? sourceMap.pointer(label, entryRange, SourceTarget{reader.range(*target, 1)})
                          : sourceMap.entry(label, entryRange);
    annotation.kind("cps-sequence-pointer")
        .parent(table)
        .derived("sequence_index", index)
        .field("encoded_pointer", entryRange, encoded, SourceValueDisplay::Hex);
    if (target) {
      annotation.derived("target_address", *target, SourceValueDisplay::Address);
    }
  }
}

void annotateSampleInfoTable(SourceMapBuilder& sourceMap, ByteReader reader, const CpsLayout& layout,
                             SourceAnnotationId table) {
  const u64 rowSize = isCps3(layout.version) ? 16 : 8;
  for (u64 relative = 0, index = 0; relative < layout.sampleInfoTableLength; relative += rowSize, ++index) {
    const u64 row = layout.sampleInfoTableOffset + relative;
    auto entry = sourceMap.entry(fmt::format("Sample Info {}", index), reader.range(row, rowSize))
                     .kind("cps-qsound-sample-info")
                     .parent(table)
                     .fieldsAsChildren()
                     .derived("sample_index", index);
    if (isCps3(layout.version)) {
      entry.field("start_address", reader.range(row, 4), reader.be32(row), SourceValueDisplay::Address)
          .field("loop_address", reader.range(row + 4, 4), reader.be32(row + 4), SourceValueDisplay::Address)
          .field("end_address", reader.range(row + 8, 4), reader.be32(row + 8), SourceValueDisplay::Address)
          .field("unity_key", reader.range(row + 12, 4), reader.be32(row + 12), SourceValueDisplay::MidiNote);
    } else {
      entry.field("bank", reader.range(row, 1), reader.u8At(row), SourceValueDisplay::Hex)
          .field("start_offset", reader.range(row + 1, 2), reader.le16(row + 1), SourceValueDisplay::Hex)
          .field("loop_offset", reader.range(row + 3, 2), reader.le16(row + 3), SourceValueDisplay::Hex)
          .field("end_offset", reader.range(row + 5, 2), reader.le16(row + 5), SourceValueDisplay::Hex)
          .field("unity_key", reader.range(row + 7, 1), reader.u8At(row + 7), SourceValueDisplay::MidiNote);
    }
  }
}

void annotateArticulationTable(SourceMapBuilder& sourceMap, ByteReader reader, const CpsLayout& layout,
                               SourceAnnotationId table) {
  constexpr u64 rowSize = 8;
  for (u64 relative = 0, index = 0; relative < layout.articulationTableLength; relative += rowSize, ++index) {
    const u64 row = *layout.articulationTableOffset + relative;
    const u32 first = reader.be32(row);
    const u32 second = reader.be32(row + 4);
    if ((first == 0 && second == 0) || (first == 0xffffffff && second == 0xffffffff)) {
      continue;
    }
    const u32 unknown = (static_cast<u32>(reader.u8At(row + 5)) << 16) | (static_cast<u32>(reader.u8At(row + 6)) << 8) |
                        reader.u8At(row + 7);
    sourceMap.entry(fmt::format("Articulation {}", index), reader.range(row, rowSize))
        .kind("cps-qsound-articulation")
        .parent(table)
        .fieldsAsChildren()
        .derived("articulation_index", index)
        .field("attack_rate", reader.range(row, 1), reader.u8At(row))
        .field("decay_rate", reader.range(row + 1, 1), reader.u8At(row + 1))
        .field("sustain_level", reader.range(row + 2, 1), reader.u8At(row + 2))
        .field("sustain_rate", reader.range(row + 3, 1), reader.u8At(row + 3))
        .field("release_rate", reader.range(row + 4, 1), reader.u8At(row + 4))
        .field("unknown", reader.range(row + 5, 3), unknown, SourceValueDisplay::Hex);
  }
}

[[nodiscard]] ScanResult scanCps(const ScanInput& input) {
  const auto format = input.source.attribute(mame::kMameFormatAttribute);
  if (format != "CPS1" && format != "CPS2") {
    return {};
  }

  ScanResultBuilder result(input, std::string(kCpsFormatName));
  auto layout = findCpsLayout(input.source, input.reader, &result.diagnostics());
  if (!layout) {
    return result.finish();
  }

  std::vector<ScanMiscAssetRef> miscAssets;
  const auto addTable = [&](std::string name, std::string_view kind, u32 offset,
                            u32 size) -> std::optional<SourceAnnotationId> {
    if (size == 0 || !input.reader.has(offset, size)) {
      return std::nullopt;
    }
    const SourceRange range = input.reader.range(offset, size);
    const auto bytes = input.reader.slice(offset, size);
    const auto misc = result.misc(name, range).payload(std::vector<u8>(bytes.begin(), bytes.end()));
    const SourceAnnotationId table =
        result.sourceMap().table(name, range).owner(ObjectRefs::misc(misc.id())).kind(kind).id();
    miscAssets.push_back(misc.ref());
    return table;
  };

  if (const auto table = addTable(layout->game + " Sequence Pointer Table", "cps-sequence-pointer-table",
                                  layout->sequenceTableOffset, layout->sequenceTableLength)) {
    annotateSequenceTable(result.sourceMap(), input.reader, *layout, *table);
  }
  if (!isCps1(layout->version)) {
    if (const auto table = addTable(layout->game + " QSound Sample Info Table", "cps-qsound-sample-info-table",
                                    layout->sampleInfoTableOffset, layout->sampleInfoTableLength)) {
      annotateSampleInfoTable(result.sourceMap(), input.reader, *layout, *table);
    }
    if (layout->articulationTableOffset) {
      if (const auto table = addTable(layout->game + " QSound Articulation Table", "cps-qsound-articulation-table",
                                      *layout->articulationTableOffset, layout->articulationTableLength)) {
        annotateArticulationTable(result.sourceMap(), input.reader, *layout, *table);
      }
    }
  }

  Cps1SynthRefs cps1Synth;
  std::optional<ScanSynthRefs> qsoundSynth;
  if (isCps1(layout->version)) {
    cps1Synth = addCps1Synth(result, *layout);
  } else {
    qsoundSynth = addCpsQSoundSynth(result, *layout);
  }

  for (const auto& sourceSequence : layout->sequences) {
    auto sequence = result.sequence(sourceSequence.name, input.reader.range(sourceSequence.offset, 1));
    sequence.program(decodeCpsSequence(input.reader, *layout, sourceSequence, sequence.id(), &result.sourceMap(),
                                       &result.diagnostics()));

    auto collection = result.collection(sourceSequence.name, collectionKey(input.source.id, sourceSequence.index));
    collection.sequence(sequence);
    if (isCps1(layout->version)) {
      if (cps1Synth.ym2151) {
        collection.instrumentSet(*cps1Synth.ym2151);
      }
      if (cps1Synth.oki) {
        collection.instrumentSet(cps1Synth.oki->instruments).samples(cps1Synth.oki->samples);
      }
    } else if (qsoundSynth) {
      collection.instrumentSet(qsoundSynth->instruments).samples(qsoundSynth->samples);
    }
    for (const auto misc : miscAssets) {
      collection.misc(misc);
    }
  }
  return result.finish();
}

}  // namespace

FormatDefinition cpsDefinition() {
  return FormatDefinition{
      .module =
          {
              .name = std::string(kCpsFormatName),
              .scan = scanCps,
          },
      .sequenceDialects = {cps1V1Dialect(), cpsEarlyDialect(), cpsLateDialect()},
  };
}

}  // namespace vgmtrans::formats::cps

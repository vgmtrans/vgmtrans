/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include <fmt/format.h>

namespace vgmtrans::formats::namco_snes {

using namespace core;

namespace {

ScanMiscDraft misc(ScanResultBuilder& result, std::string name, SourceRange range) {
  const auto bytes = result.reader().slice(range.offset, range.size);
  return result.misc(std::move(name), range).payload({bytes.begin(), bytes.end()});
}

AnnotationBuilder pointer(ByteReader reader, SourceMapBuilder& sourceMap, std::string label, u32 address) {
  const SourceRange range = reader.range(address, 2);
  const u16 destination = reader.le16(address);
  auto annotation = destination == 0 ? sourceMap.entry(label, range)
                                     : sourceMap.pointer(label, range, reader.range(destination, 1));
  return annotation.field("destination", range, destination, SourceValueDisplay::Address);
}

void addPointerTable(ByteReader reader, SourceMapBuilder& sourceMap, SourceAnnotationId parent,
                     std::string_view name, std::string_view itemName, u16 begin, u16 end,
                     bool packedEntries = false) {
  const u16 data = reader.le16(begin);
  if (data <= begin || data > end || ((data - begin) & 1) != 0) {
    return;
  }
  const auto root = sourceMap.section(name, reader.range(begin, end - begin)).parent(parent).id();
  const auto pointers = sourceMap.table("Pointers", reader.range(begin, data - begin)).parent(root).id();
  const auto entries = sourceMap.annotation(SourceRole::DataBlock, "Data", reader.range(data, end - data))
                           .parent(root).id();
  for (u32 index = 0, row = begin; row < data; ++index, row += 2) {
    const std::string label = std::string(itemName) + " " + std::to_string(index);
    pointer(reader, sourceMap, label, row).parent(pointers);
    const u16 entry = reader.le16(row);
    if (!packedEntries || entry < data || entry >= end ||
        (row > begin && reader.le16(row - 2) == entry)) {
      continue;
    }
    // Packed entries follow pointer order; consecutive aliases share one curve.
    u32 nextRow = row + 2;
    while (nextRow < data && reader.le16(nextRow) == entry) {
      nextRow += 2;
    }
    const u16 next = nextRow < data ? reader.le16(nextRow) : end;
    if (next > entry && next <= end) {
      sourceMap.entry(label, reader.range(entry, next - entry)).parent(entries);
    }
  }
}

void addSongDirectory(ByteReader reader, SourceMapBuilder& sourceMap, SourceAnnotationId parent,
                      u32 begin, u32 end) {
  const SourceRange range = reader.range(begin, end - begin);
  const auto songs = sourceMap.table("Song Directory", range).parent(parent).id();
  for (u32 song = 0, row = begin; row + 3 <= end; ++song, row += 3) {
    auto entry = sourceMap.entry("Song " + std::to_string(song), reader.range(row, 3))
                     .parent(songs).fieldsAsChildren()
                     .field("driver_slot", reader.range(row, 1), reader.u8At(row));
    pointer(reader, sourceMap, "Event Data Pointer", row + 1).parent(entry.id());
  }
}

void addDriverData(ScanResultBuilder& result, const Layout& layout, ScanCollectionBuilder& collection) {
  const ByteReader reader = result.reader();
  const u16 begin = layout.dataPointerBlockAddress;
  const u16 envelopes = layout.envelopePointerTable(reader);
  const u16 pitchEnvelopes = layout.pitchEnvelopePointerTable(reader);
  const u16 percussion = layout.percussionTable(reader);
  const u16 filters = layout.echoFilterTable(reader);
  constexpr u32 kFilterCount = 4;
  const u32 end = filters + kFilterCount * 8;
  const bool indirectSongs = layout.sequenceReferenceSize == 2;
  if (begin + (indirectSongs ? 16 : 8) > envelopes || envelopes > pitchEnvelopes ||
      pitchEnvelopes > percussion || percussion > filters || !reader.has(begin, end - begin)) {
    return;
  }

  const SourceRange range = reader.range(begin, end - begin);
  auto asset = misc(result, "Driver Data", range);
  auto& sourceMap = result.sourceMap();
  const auto root = sourceMap.section("Driver Data", range).owner(ObjectRefs::misc(asset.id())).id();
  const auto resources = sourceMap.table("Resource Pointers", reader.range(begin, 8)).parent(root).id();
  pointer(reader, sourceMap, "Envelope Presets Pointer", begin).parent(resources);
  pointer(reader, sourceMap, "Pitch Envelopes Pointer", begin + 2).parent(resources);
  pointer(reader, sourceMap, "Percussion Table Pointer", begin + 4).parent(resources);
  pointer(reader, sourceMap, "Echo FIR Presets Pointer", begin + 6).parent(resources);

  if (!indirectSongs) {
    addSongDirectory(reader, sourceMap, root, begin + 8, envelopes);
  } else {
    const auto lists = sourceMap.table("Song List Pointers", reader.range(begin + 8, 8)).parent(root).id();
    for (u32 slot = 0; slot < 4; ++slot) {
      pointer(reader, sourceMap, "Slot " + std::to_string(slot), begin + 8 + slot * 2).parent(lists);
    }

    const SourceRange entryRange = reader.range(layout.sequenceReferenceAddress, 2);
    auto entry = sourceMap.entry("Selected Song Entry", entryRange);
    if (entryRange.offset >= range.offset && entryRange.endOffset() <= range.endOffset()) {
      entry.parent(root);
    } else {
      auto entryAsset = misc(result, "Song Entry", entryRange);
      entry.owner(ObjectRefs::misc(entryAsset.id()));
      collection.misc(entryAsset);
    }
    pointer(reader, sourceMap, "Event Data Pointer", entryRange.offset).parent(entry.id());
  }
  addPointerTable(reader, sourceMap, root, "Envelope Presets", "Preset", envelopes, pitchEnvelopes);
  addPointerTable(reader, sourceMap, root, "Pitch Envelopes", "Envelope", pitchEnvelopes, percussion, true);
  sourceMap.table("Echo FIR Presets", reader.range(filters, kFilterCount * 8))
      .kind("namco-snes-echo-fir-presets")
      .parent(root);
  collection.misc(asset);
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "NamcoSnes");
  const std::string name = fmt::format("{} ({})", result.sourceDisplayName(), versionName(layout->version));
  auto sequence = result.sequence(name);
  SequenceParse parsed =
      decodeSequence(input.retain(), *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceSourceRange(input.reader, input.reader.range(layout->sequenceAddress, 1), parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(name).sequence(sequence);
  addDriverData(result, *layout, collection);
  if (const auto synth = addSynth(result, *layout, parsed.srcns, parsed.percussion, parsed.noiseRates, name)) {
    collection.soundBank(*synth);
  } else {
    result.warning("NamcoSnes sequence found, but no valid instruments or samples were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{.name = "NamcoSnes",
                      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                      .acceptedFormats = {source_formats::kSnesAram},
                      .scan = scan};
}

}  // namespace vgmtrans::formats::namco_snes

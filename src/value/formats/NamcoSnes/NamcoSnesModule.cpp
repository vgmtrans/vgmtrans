/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include <fmt/format.h>

#include <map>

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

void addEnvelopeTable(ByteReader reader, SourceMapBuilder& sourceMap, ObjectRef owner,
                      std::string_view name, u16 begin, u16 end) {
  const u16 data = reader.le16(begin);
  if (data <= begin || data > end || ((data - begin) & 1) != 0) {
    return;
  }
  const auto root = sourceMap.section(name, reader.range(begin, end - begin)).owner(owner).id();
  const auto pointers = sourceMap.table("Pointers", reader.range(begin, data - begin)).parent(root).id();
  const auto dataBlock = sourceMap.annotation(SourceRole::DataBlock, "Data", reader.range(data, end - data))
                             .parent(root).id();
  // Address order supplies script boundaries while collapsing aliases.
  std::map<u16, u32> targets;
  for (u32 index = 0, row = begin; row < data; ++index, row += 2) {
    const std::string label = "Envelope " + std::to_string(index);
    pointer(reader, sourceMap, label, row).parent(pointers);
    const u16 entry = reader.le16(row);
    if (entry >= data && entry < end) {
      targets.try_emplace(entry, index);
    }
  }
  for (auto target = targets.begin(); target != targets.end();) {
    const auto current = target++;
    const u16 limit = target != targets.end() ? target->first : end;
    sourceMap.entry("Envelope " + std::to_string(current->second),
                    reader.range(current->first, limit - current->first))
        .parent(dataBlock);
  }
}

void addSongDirectory(ByteReader reader, SourceMapBuilder& sourceMap, ObjectRef owner,
                      u32 begin, u32 end) {
  const SourceRange range = reader.range(begin, end - begin);
  const auto songs = sourceMap.table("Song Directory", range).owner(owner).id();
  for (u32 song = 0, row = begin; row + 3 <= end; ++song, row += 3) {
    auto entry = sourceMap.entry("Song " + std::to_string(song), reader.range(row, 3))
                     .parent(songs).fieldsAsChildren()
                     .field("driver_slot", reader.range(row, 1), reader.u8At(row));
    pointer(reader, sourceMap, "Event Data Pointer", row + 1).parent(entry.id());
  }
}

void addPercussionTable(ByteReader reader, SourceMapBuilder& sourceMap, ObjectRef owner,
                        u32 begin, u32 end, const std::set<u8>& used) {
  const auto table = sourceMap.table("Percussion Table", reader.range(begin, end - begin))
                         .kind("namco-snes-percussion-table")
                         .description("Each row maps its percussion key to a sample, amplitude envelope, mix, and "
                                      "source key; $55-$7F source keys select DSP noise")
                         .owner(owner)
                         .id();
  for (u32 index = 0, row = begin; row + 5 <= end; ++index, row += 5) {
    const auto byte = [&](u32 offset) { return reader.range(row + offset, 1); };
    sourceMap.entry(fmt::format("Percussion {}{}", index, used.contains(index) ? "" : " (unused)"),
                    reader.range(row, 5))
        .kind("namco-snes-percussion")
        .parent(table)
        .fieldsAsChildren()
        .field("sample_srcn", byte(0), reader.u8At(row))
        .field("amplitude_envelope", byte(1), reader.u8At(row + 1))
        .field("volume", byte(2), reader.u8At(row + 2), SourceValueDisplay::Hex)
        .field("stereo_balance", byte(3), reader.u8At(row + 3), SourceValueDisplay::Hex)
        .field("source_key", byte(4), reader.u8At(row + 4), SourceValueDisplay::Hex);
  }
}

void addSongTables(ScanResultBuilder& result, const Layout& layout,
                   const std::set<u8>& usedPercussion, ScanCollectionBuilder& collection) {
  const ByteReader reader = result.reader();
  const u16 begin = layout.dataPointerBlockAddress;
  const u16 amplitudeEnvelopes = layout.amplitudeEnvelopePointerTable(reader);
  const u16 pitchEnvelopes = layout.pitchEnvelopePointerTable(reader);
  const u16 percussion = layout.percussionTable(reader);
  const u16 filters = layout.echoFilterTable(reader);
  constexpr u32 kFilterCount = 4;
  const u32 end = filters + kFilterCount * 8;
  const bool indirectSongs = layout.sequenceReferenceSize == 2;
  if (begin + (indirectSongs ? 16 : 8) > amplitudeEnvelopes || amplitudeEnvelopes > pitchEnvelopes ||
      pitchEnvelopes > percussion || percussion > filters || !reader.has(begin, end - begin)) {
    return;
  }

  const SourceRange range = reader.range(begin, end - begin);
  auto asset = misc(result, "Song Tables", range);
  auto& sourceMap = result.sourceMap();
  const ObjectRef owner = ObjectRefs::misc(asset.id());
  const auto resources = sourceMap.table("Resource Pointers", reader.range(begin, 8)).owner(owner).id();
  pointer(reader, sourceMap, "Amplitude Envelopes Pointer", begin).parent(resources);
  pointer(reader, sourceMap, "Pitch Envelopes Pointer", begin + 2).parent(resources);
  pointer(reader, sourceMap, "Percussion Table Pointer", begin + 4).parent(resources);
  pointer(reader, sourceMap, "Echo FIR Presets Pointer", begin + 6).parent(resources);

  if (!indirectSongs) {
    addSongDirectory(reader, sourceMap, owner, begin + 8, amplitudeEnvelopes);
  } else {
    const auto lists = sourceMap.table("Song List Pointers", reader.range(begin + 8, 8)).owner(owner).id();
    for (u32 slot = 0; slot < 4; ++slot) {
      pointer(reader, sourceMap, "Slot " + std::to_string(slot), begin + 8 + slot * 2).parent(lists);
    }

    const SourceRange entryRange = reader.range(layout.sequenceReferenceAddress, 2);
    auto entry = sourceMap.entry("Selected Song Entry", entryRange);
    if (entryRange.offset >= range.offset && entryRange.endOffset() <= range.endOffset()) {
      entry.owner(owner);
    } else {
      auto entryAsset = misc(result, "Song Entry", entryRange);
      entry.owner(ObjectRefs::misc(entryAsset.id()));
      collection.misc(entryAsset);
    }
    pointer(reader, sourceMap, "Event Data Pointer", entryRange.offset).parent(entry.id());
  }
  addEnvelopeTable(reader, sourceMap, owner, "Amplitude Envelopes", amplitudeEnvelopes, pitchEnvelopes);
  addEnvelopeTable(reader, sourceMap, owner, "Pitch Envelopes", pitchEnvelopes, percussion);
  addPercussionTable(reader, sourceMap, owner, percussion, filters, usedPercussion);
  const auto fir = sourceMap.table("Echo FIR Presets", reader.range(filters, kFilterCount * 8))
                       .kind("namco-snes-echo-fir-presets")
                       .owner(owner)
                       .id();
  for (u32 preset = 0; preset < kFilterCount; ++preset) {
    const u32 address = filters + preset * 8;
    auto entry = sourceMap.entry("Preset " + std::to_string(preset), reader.range(address, 8))
                     .parent(fir)
                     .fieldsAsChildren();
    for (u32 coefficient = 0; coefficient < 8; ++coefficient) {
      entry.field("coefficient_" + std::to_string(coefficient), reader.range(address + coefficient, 1),
                  static_cast<s8>(reader.u8At(address + coefficient)), SourceValueDisplay::SignedDecimal);
    }
  }
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
  addSongTables(result, *layout, parsed.percussion, collection);
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

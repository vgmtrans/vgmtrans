/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiTMNT2/KonamiTMNT2.h"

#include "value/extractors/MameRomSetExtractor.h"

#include <algorithm>
#include <string>
#include <vector>

namespace vgmtrans::formats::konami_tmnt2 {

using namespace core;

namespace {

[[nodiscard]] CollectionKey collectionKey(SourceId source, u32 sequence) {
  return CollectionKey{
      .resolver = std::string(kFormatName),
      .value = "source:" + std::to_string(source.value) + ":sequence:" + std::to_string(sequence),
  };
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  if (input.source.attribute(mame::kMameFormatAttribute) != kFormatName) {
    return {};
  }
  ScanResultBuilder result(input, std::string(kFormatName));
  const auto layout = findLayout(input.source, input.reader, &result.diagnostics());
  if (!layout) {
    return result.finish();
  }

  u64 sequenceTableEnd = layout->sequenceTable.endOffset();
  for (const auto& sequence : layout->sequences) {
    sequenceTableEnd = std::max(sequenceTableEnd, sequence.trackTable.endOffset());
  }
  const SourceRange sequenceTableRange{
      .source = layout->sequenceTable.source,
      .offset = layout->sequenceTable.offset,
      .size = sequenceTableEnd - layout->sequenceTable.offset,
  };
  const auto sequenceTableBytes = input.reader.slice(sequenceTableRange.offset, sequenceTableRange.size);
  auto sequenceTable = result.misc("Sequence Table", sequenceTableRange)
                           .payload(std::vector<u8>(sequenceTableBytes.begin(), sequenceTableBytes.end()));
  const SourceAnnotationId sequenceTableRoot = result.sourceMap()
                                                   .table("Sequence Table", sequenceTableRange)
                                                   .owner(ObjectRefs::misc(sequenceTable.id()))
                                                   .kind("konami-tmnt2-sequence-table")
                                                   .id();
  const SourceAnnotationId pointerTable = result.sourceMap()
                                              .table("Sequence Pointer Table", layout->sequenceTable)
                                              .parent(sequenceTableRoot)
                                              .kind("konami-tmnt2-sequence-pointer-table")
                                              .id();
  for (const auto& pointer : layout->sequencePointers) {
    const auto sequence = std::ranges::find(layout->sequences, pointer.sequenceIndex, &SequenceLayout::index);
    result.sourceMap()
        .pointer("Sequence Pointer " + std::to_string(pointer.slot), pointer.range,
                 sequence != layout->sequences.end()
                     ? SourceTarget{sequence->trackTable}
                     : SourceTarget{input.reader.range(layout->program.offset + pointer.encoded, 1)})
        .parent(pointerTable)
        .kind("konami-tmnt2-sequence-pointer")
        .fieldsAsChildren()
        .field("address", pointer.range, pointer.encoded, SourceValueDisplay::Address)
        .derived("sequence_index", pointer.sequenceIndex);
  }

  const auto synth = addSynth(result, *layout);
  for (const auto& sourceSequence : layout->sequences) {
    const auto firstTrack = std::ranges::min_element(sourceSequence.tracks, {}, &TrackLayout::offset);
    auto sequence = result.sequence(sourceSequence.name, input.reader.range(firstTrack->offset, 1));
    sequence.program(decodeSequence(input.reader, *layout, sourceSequence, sequence.id(), &result.sourceMap(),
                                    &result.diagnostics()));
    const SourceAnnotationId trackTable = result.sourceMap()
                                              .entry(sourceSequence.name + " Track Table", sourceSequence.trackTable)
                                              .owner(ObjectRefs::misc(sequenceTable.id()))
                                              .parent(sequenceTableRoot)
                                              .kind("konami-tmnt2-sequence-track-table")
                                              .id();
    const u32 typeSize = layout->version == Version::Vendetta ? 0 : 1;
    if (typeSize != 0) {
      result.sourceMap()
          .field("Sequence Type", input.reader.range(sourceSequence.trackTable.offset, 1),
                 input.reader.u8At(sourceSequence.trackTable.offset))
          .parent(trackTable)
          .kind("konami-tmnt2-sequence-type");
    }
    for (u32 track = 0; track < sourceSequence.totalTrackCount; ++track) {
      const u32 offset = static_cast<u32>(sourceSequence.trackTable.offset + typeSize + track * 2);
      const u16 encoded = input.reader.le16(offset);
      const std::string label = sourceSequence.trackName(track) + " Pointer";
      const u32 target = static_cast<u32>(layout->program.offset + encoded);
      const SourceRange range = input.reader.range(offset, 2);
      auto pointer = encoded != 0 && input.reader.has(target, 1)
                         ? result.sourceMap().pointer(label, range, input.reader.range(target, 1))
                         : result.sourceMap().entry(label, range);
      pointer.parent(trackTable)
          .kind("konami-tmnt2-track-pointer")
          .field("address", range, encoded, SourceValueDisplay::Address)
          .derived("track", track);
      if (encoded == 0) {
        pointer.description("Unused track");
      }
    }
    auto collection = result.collection(sourceSequence.name, collectionKey(input.source.id, sourceSequence.index));
    collection.sequence(sequence);
    collection.misc(sequenceTable);
    for (const auto& bank : synth) {
      collection.soundBank(bank);
    }
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{
      .name = std::string(kFormatName),
      .acceptedFormats = {source_formats::kKonamiTMNT2},
      .scan = scan,
  };
}

}  // namespace vgmtrans::formats::konami_tmnt2

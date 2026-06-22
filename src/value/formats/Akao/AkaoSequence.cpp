/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoSequence.h"

#include "value/formats/Akao/AkaoSequenceDecoder.h"
#include "value/formats/Akao/AkaoVersion.h"

#include <algorithm>
#include <bit>
#include <fmt/format.h>

namespace vgmtrans::formats::akao {

using namespace core;

bool isPossibleAkaoSequence(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 0x10) || reader.be32(offset) != kAkaoSignature || reader.le16(offset + 6) == 0) {
    return false;
  }
  const AkaoPs1Version version = guessSequenceVersion(reader, offset);
  const AkaoProfile profile = akaoProfile(version);
  const u32 bitsOffset = profile.trackAllocationBitsOffset();
  if (!reader.has(offset + bitsOffset, 4)) {
    return false;
  }
  const u32 trackBits = reader.le32(offset + bitsOffset);
  if (!profile.version3OrLater() && (trackBits & ~0xffffffu) != 0) {
    return false;
  }
  if (profile.version3OrLater()) {
    if (!reader.has(offset + 0x40, 1)) {
      return false;
    }
    if (reader.le32(offset + 0x28) != 0 || reader.le32(offset + 0x2c) != 0 || reader.le32(offset + 0x38) != 0 ||
        reader.le32(offset + 0x3c) != 0) {
      return false;
    }
  }
  return true;
}

std::optional<AkaoSequenceAnalysis> analyzeAkaoSequence(ByteReader reader, const SourceFile& source, u32 offset) {
  if (!isPossibleAkaoSequence(reader, offset)) {
    return std::nullopt;
  }
  AkaoPs1Version version = determineVersionFromSource(source);
  if (version == AkaoPs1Version::Unknown) {
    version = guessSequenceVersion(reader, offset);
  }
  if (version == AkaoPs1Version::Unknown) {
    return std::nullopt;
  }

  const AkaoProfile profile = akaoProfile(version);
  const u32 length = profile.sequenceLength(reader, offset);
  if (length == 0 || !reader.has(offset, std::min<u64>(length, reader.size() - offset))) {
    return std::nullopt;
  }

  AkaoSequenceAnalysis analysis;
  analysis.header = AkaoSequenceHeader{
      .offset = offset,
      .length = static_cast<u32>(std::min<u64>(length, reader.size() - offset)),
      .version = version,
      .sequenceId = reader.le16(offset + 4),
      .trackBits = reader.le32(offset + profile.trackAllocationBitsOffset()),
      .trackHeaderOffset = profile.trackHeaderOffset(),
  };
  if (profile.version3OrLater()) {
    analysis.header.sampleSetId = reader.le16(offset + 0x14);
    const u32 instr = reader.le32(offset + 0x30);
    const u32 drum = reader.le32(offset + 0x34);
    if (instr != 0) {
      analysis.header.instrumentSetOffset = offset + 0x30 + instr;
    }
    if (drum != 0) {
      analysis.header.drumSetOffset = offset + 0x34 + drum;
    }
  }

  const u32 trackCount = std::popcount(analysis.header.trackBits);
  const u32 pointerTable = offset + analysis.header.trackHeaderOffset;
  const u32 sequenceEnd = offset + analysis.header.length;
  if (!reader.has(pointerTable, trackCount * 2ull)) {
    return std::nullopt;
  }

  for (u32 i = 0; i < trackCount; ++i) {
    const u32 pointerOffset = analysis.header.trackHeaderOffset + i * 2;
    const u32 base = pointerOffset + (profile.version3OrLater() ? 0 : 2);
    const u32 relative = reader.le16(offset + pointerOffset);
    const u32 trackStart = offset + base + relative;
    if (trackStart < sequenceEnd && reader.has(trackStart, 1)) {
      analysis.trackStarts.push_back(trackStart);
    }
  }

  for (const u32 trackStart : analysis.trackStarts) {
    analyzeAkaoTrack(reader, analysis, trackStart);
  }
  return analysis;
}

SequenceProgramAsset parseAkaoSequenceProgram(const ScanInput& input, AssetId id, const AkaoSequenceAnalysis& analysis,
                                              std::optional<ScanInstrumentSetRef> instrumentSet,
                                              SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const SequenceDialect dialect = makeAkaoDialect(analysis.header.version);
  const u32 sequenceEnd = analysis.header.offset + analysis.header.length;
  const std::string name = fmt::format("Akao Seq {:02X}", analysis.header.sequenceId);
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{analysis.header.offset},
      .behavior = dialect.defaultBehavior,
  };

  ItemTree items;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const ItemId root = itemBuilder.add(std::nullopt, ItemKind::Sequence, "akao-sequence", name,
                                      input.reader.range(analysis.header.offset, analysis.header.length));

  if (sourceMap != nullptr) {
    auto header =
        sourceMap
            ->header("AKAO Sequence Header",
                     input.reader.range(analysis.header.offset, analysis.header.trackHeaderOffset))
            .kind("akao-sequence-header")
            .field("sequence_id", input.reader.range(analysis.header.offset + 4, 2), analysis.header.sequenceId)
            .field("size", input.reader.range(analysis.header.offset + 6, 2), analysis.header.length)
            .field("track_bits",
                   input.reader.range(
                       analysis.header.offset + akaoProfile(analysis.header.version).trackAllocationBitsOffset(), 4),
                   analysis.header.trackBits, SourceValueDisplay::Hex);
    if (analysis.header.sampleSetId) {
      header.field("sample_set_id", input.reader.range(analysis.header.offset + 0x14, 2), *analysis.header.sampleSetId);
    }
  }

  const std::optional<AssetId> instrumentSetId =
      instrumentSet ? std::optional<AssetId>{instrumentSet->id} : std::nullopt;
  u32 trackIndex = 0;
  for (const u32 start : analysis.trackStarts) {
    auto track = decodeAkaoTrack(input.reader, dialect,
                                 CursorTrackDecodeInput{
                                     .trackIndex = trackIndex,
                                     .startOffset = start,
                                     .bytecodeEnd = sequenceEnd,
                                     .sequenceOffset = analysis.header.offset,
                                     .sequenceEnd = sequenceEnd,
                                     .sourceMap = sourceMap,
                                     .diagnostics = diagnostics,
                                     .maxCommands = kAkaoMaxTrackCommands,
                                 });
    track.sourceTrackNumber = trackIndex;
    const auto trackItem = itemBuilder.add(root, ItemKind::Track, "akao-track", fmt::format("Track {}", trackIndex + 1),
                                           input.reader.range(start, 0));
    addSourceCommandItemsAndInstrumentReferences(itemBuilder, trackItem, program, dialect, track, instrumentSetId);
    program.tracks.push_back(std::move(track));
    ++trackIndex;
  }

  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kAkaoFormatName),
              .name = name,
              .range = input.reader.range(analysis.header.offset, analysis.header.length),
              .items = std::move(items),
          },
      .program = std::move(program),
  };
}

void registerAkaoSequenceDialects(SequenceDialectRegistry& registry) {
  registry.add(makeAkaoDialect(AkaoPs1Version::Version1_0));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version1_1));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version1_2));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version2));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version3_0));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version3_1));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version3_2));
}

}  // namespace vgmtrans::formats::akao

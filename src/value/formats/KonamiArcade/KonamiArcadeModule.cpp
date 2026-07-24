/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiArcade/KonamiArcade.h"

#include "value/extractors/MameRomSetExtractor.h"

#include <string>

namespace vgmtrans::formats::konami_arcade {

using namespace core;

namespace {

[[nodiscard]] bool canScanKonamiArcade(const SourceFile& source, std::span<const u8>) {
  return source.attribute(mame::kMameFormatAttribute) == kKonamiArcadeFormatName;
}

[[nodiscard]] CollectionKey collectionKey(SourceId source, u32 sequenceIndex) {
  return CollectionKey{
      .resolver = std::string(kKonamiArcadeFormatName),
      .value = "source:" + std::to_string(source.value) + ":sequence:" + std::to_string(sequenceIndex),
  };
}

[[nodiscard]] ScanResult scanKonamiArcade(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kKonamiArcadeFormatName));
  const auto layout = findKonamiArcadeLayout(input.source, input.reader, &result.diagnostics());
  if (!layout) {
    return result.finish();
  }

  const auto instruments = result.reserveInstrumentSet();
  const auto samples = result.reserveSampleCollection();
  const bool hasSynth = addKonamiArcadeSynth(result, instruments, samples, *layout);
  if (!hasSynth) {
    result.warning("KonamiArcade sequences found, but no valid instruments or samples were discovered", layout->code);
  }

  for (const auto& sourceSequence : layout->sequences) {
    const u32 pointerSize = layout->version == KonamiArcadeVersion::MysticWarrior ? 2 : 4;
    const u32 maximumHeaderSize = pointerSize * kKonamiArcadeMaxTracks;
    const u32 available = sourceSequence.offset < layout->code.endOffset()
                              ? static_cast<u32>(layout->code.endOffset() - sourceSequence.offset)
                              : 0;
    const SourceRange sequenceRange = input.reader.range(sourceSequence.offset, std::min(maximumHeaderSize, available));
    const auto sequence = result.reserveSequence();
    result.sequence(sequence, sourceSequence.name, sequenceRange)
        .program(decodeKonamiArcadeSequence(input.reader, *layout, sourceSequence, sequence.id, &result.sourceMap(),
                                            &result.diagnostics()));

    auto collection = result.collection(sourceSequence.name, collectionKey(input.source.id, sourceSequence.index));
    collection.sequence(sequence);
    if (hasSynth) {
      collection.instrumentSet(instruments).samples(samples);
    }

    result.sourceFact(sequence.id,
                      FormatSpecificFact{
                          .kind = "konami-arcade-sequence",
                          .fields =
                              {
                                  MatchField{.name = "game", .value = layout->game},
                                  MatchField{.name = "version", .value = konamiArcadeVersionName(layout->version)},
                                  MatchField{.name = "index", .value = std::to_string(sourceSequence.index)},
                              },
                      });
  }

  return result.finish();
}

}  // namespace

FormatDefinition konamiArcadeDefinition() {
  return FormatDefinition{
      .module =
          {
              .name = std::string(kKonamiArcadeFormatName),
              .canScan = canScanKonamiArcade,
              .scan = scanKonamiArcade,
          },
      .sequenceDialects = {konamiArcadeSequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::konami_arcade

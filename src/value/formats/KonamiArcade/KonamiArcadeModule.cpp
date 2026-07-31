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

[[nodiscard]] CollectionKey collectionKey(SourceId source, u32 sequenceIndex) {
  return CollectionKey{
      .resolver = std::string(kKonamiArcadeFormatName),
      .value = "source:" + std::to_string(source.value) + ":sequence:" + std::to_string(sequenceIndex),
  };
}

[[nodiscard]] ScanResult scanKonamiArcade(const ScanInput& input) {
  if (input.source.attribute(mame::kMameFormatAttribute) != kKonamiArcadeFormatName) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kKonamiArcadeFormatName));
  const auto layout = findKonamiArcadeLayout(input.source, input.reader, &result.diagnostics());
  if (!layout) {
    return result.finish();
  }

  const auto synth = addKonamiArcadeSynth(result, *layout);

  for (const auto& sourceSequence : layout->sequences) {
    auto sequence = result.sequence(sourceSequence.name, sourceSequence.trackTable);
    sequence.program(decodeKonamiArcadeSequence(input.reader, *layout, sourceSequence, sequence.id(),
                                                &result.sourceMap(), &result.diagnostics()));

    auto collection = result.collection(sourceSequence.name, collectionKey(input.source.id, sourceSequence.index));
    collection.sequence(sequence);
    collection.instrumentSet(synth.instruments).samples(synth.samples);
  }

  return result.finish();
}

}  // namespace

FormatDefinition konamiArcadeDefinition() {
  return FormatDefinition{
      .module =
          {
              .name = std::string(kKonamiArcadeFormatName),
              .scan = scanKonamiArcade,
          },
      .sequenceDialects = {konamiArcadeSequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::konami_arcade

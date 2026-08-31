/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiTMNT2/KonamiTMNT2.h"

#include "value/extractors/MameRomSetExtractor.h"

#include <string>

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
  const auto synth = addSynth(result, *layout);
  for (const auto& sourceSequence : layout->sequences) {
    auto sequence = result.sequence(sourceSequence.name, sourceSequence.trackTable);
    sequence.program(decodeSequence(input.reader, *layout, sourceSequence, sequence.id(), &result.sourceMap(),
                                    &result.diagnostics()));
    auto collection = result.collection(sourceSequence.name, collectionKey(input.source.id, sourceSequence.index));
    collection.sequence(sequence);
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

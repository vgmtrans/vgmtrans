/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"
#include "value/scan/ScanResultBuilder.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

[[nodiscard]] CollectionKey capcomCollectionKey(SourceId source) {
  return CollectionKey{
      .resolver = "CapcomSnes",
      .value = "source:" + std::to_string(source.value),
  };
}

[[nodiscard]] ScanResult scanCapcomSnes(const ScanInput& input) {
  const auto layout = findCapcomSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  // Keep this file as wiring: layout discovery, sequence decoding, and synth parsing each stay in their own file.
  const std::string displayName = capcomSnesSourceDisplayName(input.source);
  ScanResultBuilder result(input, "CapcomSnes");
  const auto sequence = result.reserveSequence();
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto samples = result.reserveSampleCollection();

  const u32 headerSize = (layout->priorityInHeader ? 1 : 0) + kCapcomSnesMaxTracks * 2;
  const SourceRange sequenceRange = input.reader.range(layout->sequenceHeaderAddress, headerSize);
  result.sequence(sequence, displayName, sequenceRange)
      .program(decodeCapcomSnesSequence(input.reader, *layout, sequence.id, sequenceRange, &result.sourceMap(),
                                        &result.diagnostics()));

  auto collection = result.collection(displayName, capcomCollectionKey(input.source.id));
  collection.sequence(sequence);

  const bool hasSynth = layout->instrumentTableAddress && layout->spcDirAddress &&
                        addCapcomSnesSynth(result, instrumentSet, samples, *layout->instrumentTableAddress,
                                           *layout->spcDirAddress, displayName);
  if (hasSynth) {
    collection.instrumentSet(instrumentSet).samples(samples);
  }

  if (!layout->instrumentTableAddress || !layout->spcDirAddress) {
    result.warning("CapcomSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  return result.finish();
}

FormatDefinition capcomSnesDefinition() {
  return FormatDefinition{
      .module =
          FormatModule{
              .name = "CapcomSnes",
              .scan = scanCapcomSnes,
          },
      .sequenceDialect = capcomSnesSequenceDialect(),
  };
}

}  // namespace vgmtrans::formats::capcom_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnes.h"

#include <string>

namespace vgmtrans::formats::akao_snes {

using namespace core;

[[nodiscard]] ScanResult scanAkaoSnes(const ScanInput& input) {
  const auto layout = findAkaoSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kAkaoSnesFormatName));
  const std::string displayName = result.sourceDisplayName();
  const auto sequence = result.reserveSequence();
  SequenceProgram program =
      parseAkaoSnesSequence(input.reader, *layout, sequence.id, &result.sourceMap(), &result.diagnostics());
  if (program.tracks.empty()) {
    return {};
  }
  result
      .sequence(sequence, displayName,
                input.reader.range(layout->sequenceHeaderAddress,
                                   akaoSnesSequenceHeaderSize(layout->version, layout->minorVersion)))
      .program(std::move(program));

  auto collection = result.sourceCollection(displayName);
  collection.sequence(sequence);

  const bool hasSynthLayout = layout->spcDirAddress && layout->tuningTableAddress &&
                              (layout->version == AKAOSNES_V1 || layout->adsrTableAddress);
  if (hasSynthLayout) {
    const auto instrumentSet = result.reserveInstrumentSet();
    const auto samples = result.reserveSampleCollection();
    if (addAkaoSnesSynth(result, instrumentSet, samples, *layout, displayName)) {
      collection.instrumentSet(instrumentSet).samples(samples);
    } else {
      result.warning("AkaoSnes sequence found, but no valid instruments or samples were discovered",
                     input.reader.range(0, input.reader.size()));
    }
  } else {
    result.warning("AkaoSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  result.sourceFact(
      sequence.id,
      FormatSpecificFact{
          .kind = "akao-snes-version",
          .fields =
              {
                  MatchField{.name = "version", .value = std::string(akaoSnesVersionName(layout->version))},
                  MatchField{.name = "minor", .value = std::string(akaoSnesMinorVersionName(layout->minorVersion))},
              },
      });

  return result.finish();
}

FormatDefinition akaoSnesDefinition() {
  return FormatDefinition{
      .module = {.name = std::string(kAkaoSnesFormatName), .scan = scanAkaoSnes},
      .sequenceDialects = {akaoSnesSequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::akao_snes

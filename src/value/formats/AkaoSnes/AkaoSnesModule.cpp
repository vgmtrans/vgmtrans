/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnesModule.h"

#include "value/formats/AkaoSnes/AkaoSnesLayout.h"
#include "value/formats/AkaoSnes/AkaoSnesSequence.h"
#include "value/formats/AkaoSnes/AkaoSnesSynth.h"
#include "value/scan/FormatRegistry.h"
#include "value/scan/ScanResultBuilder.h"

#include <string>

namespace vgmtrans::formats::akao_snes {

using namespace core;

[[nodiscard]] bool canScanAkaoSnes(const SourceFile&, std::span<const u8> bytes) {
  return findAkaoSnesLayout(ByteReader(SourceId{}, bytes)).has_value();
}

[[nodiscard]] ScanResult scanAkaoSnes(const ScanInput& input) {
  const auto layout = findAkaoSnesLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "AkaoSnes");
  const std::string displayName = result.sourceDisplayName();
  const auto sequence = result.reserveSequence();
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto samples = result.reserveSampleCollection();

  auto sequenceAsset =
      parseAkaoSnesSequence(input, *layout, sequence.id, displayName, &result.sourceMap(), &result.diagnostics());
  if (sequenceAsset.program.tracks.empty()) {
    return {};
  }
  result.sequence(sequence, [&](AssetId) { return std::move(sequenceAsset); });

  auto collection = result.sourceCollection(displayName);
  collection.sequence(sequence);

  const bool hasSynthLayout = layout->spcDirAddress && layout->tuningTableAddress &&
                              (layout->version == AKAOSNES_V1 || layout->adsrTableAddress);
  if (hasSynthLayout) {
    if (addAkaoSnesSynth(input, result, instrumentSet, samples, *layout, displayName)) {
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

void registerAkaoSnesModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = "AkaoSnes",
      .canScan = canScanAkaoSnes,
      .scan = scanAkaoSnes,
  });
}

}  // namespace vgmtrans::formats::akao_snes

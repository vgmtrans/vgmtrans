/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OhoriAkaPS1/OhoriAkaPS1.h"

#include <fmt/format.h>

#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::ohori_aka_ps1 {

using namespace core;

namespace {

[[nodiscard]] ScanResult scanOhoriAkaPs1(const ScanInput& input) {
  const auto layouts = findOhoriAkaPs1Sequences(input.reader);
  if (layouts.empty()) return {};

  ScanResultBuilder result(input, std::string(kOhoriAkaPs1FormatName));
  for (const auto& layout : layouts) {
    std::vector<OhoriAkaPs1Instrument> instruments;
    std::optional<ScanSoundBankDraft> bank;
    if (const auto bankLayout = findOhoriAkaPs1Bank(input.reader, layout)) {
      if (auto scanned = addOhoriAkaPs1Bank(result, *bankLayout)) {
        bank = scanned->bank;
        instruments = std::move(scanned->instruments);
      } else {
        result.warning("OhoriAkaPS1 instrument bank was recognized, but its sample data could not be located",
                       input.reader.range(bankLayout->offset, bankLayout->length));
      }
    }

    const std::string name = fmt::format("OhoriAkaPS1 Sequence {:X}", layout.offset);
    auto sequence = result.sequence(name, input.reader.range(layout.offset, layout.length));
    sequence.program(parseOhoriAkaPs1Sequence(input.reader, sequence.id(), layout, instruments, &result.sourceMap(),
                                              &result.diagnostics()));
    auto collection = result
                          .collection(name, CollectionKey{.value = fmt::format("source:{}:sequence:{}",
                                                                              result.source().value, layout.offset)})
                          .sequence(sequence);
    if (bank) collection.soundBank(*bank);
  }
  return result.finish();
}

}  // namespace

FormatModule ohoriAkaPs1Module() {
  return FormatModule{
      .name = std::string(kOhoriAkaPs1FormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .acceptedFormats = {source_formats::kPlayStationRam},
      .scan = scanOhoriAkaPs1,
  };
}

}  // namespace vgmtrans::formats::ohori_aka_ps1

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HOSA/HOSA.h"

#include <fmt/format.h>

#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::hosa {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layouts = findSequences(input.reader);
  if (layouts.empty()) return {};

  ScanResultBuilder result(input, std::string(kHosaFormatName));
  for (const auto& layout : layouts) {
    std::vector<Instrument> instruments;
    std::optional<ScanSoundBankDraft> bank;
    if (const auto bankLayout = findBank(input.reader, layout)) {
      if (auto scanned = addBank(result, *bankLayout)) {
        bank = scanned->bank;
        instruments = std::move(scanned->instruments);
      } else {
        result.warning("A HOSA instrument bank was recognized, but its sample data could not be located",
                       input.reader.range(bankLayout->offset, bankLayout->length));
      }
    }

    const std::string name = fmt::format("HOSA Sequence {:X}", layout.offset);
    auto sequence = result.sequence(name, input.reader.range(layout.offset, layout.length));
    sequence.program(parseSequence(input.reader, sequence.id(), layout, instruments, &result.sourceMap(),
                                   &result.diagnostics()));
    auto collection =
        result
            .collection(name, CollectionKey{.value = fmt::format("source:{}:sequence:{}", result.source().value,
                                                                  layout.offset)})
            .sequence(sequence);
    if (bank) collection.soundBank(*bank);
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{
      .name = std::string(kHosaFormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .acceptedFormats = {source_formats::kPlayStationRam},
      .scan = scan,
  };
}

}  // namespace vgmtrans::formats::hosa

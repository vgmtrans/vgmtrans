/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiPS1/SuzukiPS1.h"

#include <fmt/format.h>

#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::suzuki_ps1 {

using namespace core;

namespace {

[[nodiscard]] ScanResult scanSuzukiPs1(const ScanInput& input) {
  const auto bankLayouts = findSuzukiPs1Banks(input.reader);
  const auto sequenceLayouts = findSuzukiPs1Sequences(input.reader);
  if (bankLayouts.empty() && sequenceLayouts.empty()) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kSuzukiPs1FormatName));
  std::vector<SuzukiPs1ScannedBank> banks;
  std::vector<SuzukiPs1EnvelopeRegisters> envelopes;
  for (const auto& layout : bankLayouts) {
    if (auto bank = addSuzukiPs1Bank(result, layout)) {
      envelopes.insert(envelopes.end(), bank->envelopes.begin(), bank->envelopes.end());
      banks.push_back(std::move(*bank));
    } else {
      result.warning("SuzukiPS1 WDS header was recognized, but no playable instruments were found",
                     input.reader.range(layout.offset, layout.length));
    }
  }

  for (const auto& layout : sequenceLayouts) {
    const std::string name =
        layout.title.empty() ? fmt::format("SuzukiPS1 Sequence {:X}", layout.offset) : layout.title;
    auto sequence = result.sequence(name, input.reader.range(layout.offset, layout.length));
    sequence.program(parseSuzukiPs1Sequence(input.reader, sequence.id(), layout, envelopes, &result.sourceMap(),
                                            &result.diagnostics()));
    auto collection =
        result
            .collection(name,
                        CollectionKey{
                            .value = fmt::format("source:{}:sequence:{}", result.source().value, layout.offset),
                        })
            .sequence(sequence);

    // A source can contain several WDS uploads and switch between them with
    // FE. Keeping them in one collection preserves those source bank IDs.
    for (const auto& bank : banks) {
      collection.instrumentSet(bank.instruments).samples(bank.samples);
    }
  }

  return result.finish();
}

}  // namespace

FormatModule suzukiPs1Module() {
  return FormatModule{.name = std::string(kSuzukiPs1FormatName),
                      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
                      .acceptedFormats = {source_formats::kPlayStationRam},
                      .scan = scanSuzukiPs1};
}

}  // namespace vgmtrans::formats::suzuki_ps1

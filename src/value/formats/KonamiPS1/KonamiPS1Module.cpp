/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiPS1/KonamiPS1.h"

#include "value/formats/SonyPS1/SonyPS1.h"

#include <fmt/format.h>

#include <string>
#include <utility>

namespace vgmtrans::formats::konami_ps1 {

using namespace core;

namespace {

[[nodiscard]] ScanResult scanKonamiPs1(const ScanInput& input) {
  const auto layouts = findKonamiPs1Sequences(input.reader);
  if (layouts.empty()) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kKonamiPs1FormatName), std::string(kKonamiPs1CollectionResolver));
  const auto rootCounterTarget = findKonamiPs1RootCounterTarget(input.reader);
  if (!rootCounterTarget) {
    result.diagnostics().push_back(Diagnostic{
        .severity = Severity::Error,
        .message = "KonamiPS1 timer setup was not found",
        .range = input.reader.range(layouts.front().offset, 4),
    });
    return result.finish();
  }
  const auto tones = readKonamiPs1Tones(input.reader);
  for (const auto& layout : layouts) {
    const std::string name =
        layout.hasKdt2Header ? fmt::format("{} KDT {}", result.sourceDisplayName(), layout.sequenceId)
                             : fmt::format("{} KDT{} {:X}", result.sourceDisplayName(), layout.version, layout.offset);
    auto sequence = result.sequence(name, input.reader.range(layout.containerOffset, layout.containerLength));
    sequence.program(parseKonamiPs1Sequence(input.reader, sequence.id(), layout, *rootCounterTarget, tones,
                                            &result.sourceMap(), &result.diagnostics()));
  }
  return result.finish();
}

}  // namespace

FormatModule konamiPs1Module() {
  return FormatModule{
      .name = std::string(kKonamiPs1FormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .acceptedFormats = {source_formats::kPlayStationRam},
      .scan = scanKonamiPs1,
      .collectionResolverId = std::string(kKonamiPs1CollectionResolver),
      .resolveCollections = resolveKonamiPs1Collections,
      .bindCollection = sony_ps1::bindSonyPs1Collection,
  };
}

}  // namespace vgmtrans::formats::konami_ps1

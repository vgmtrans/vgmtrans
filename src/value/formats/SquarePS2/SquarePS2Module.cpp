/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SquarePS2/SquarePS2.h"

#include <fmt/format.h>

#include <string>
#include <utility>

namespace vgmtrans::formats::square_ps2 {

using namespace core;

namespace {

[[nodiscard]] ScanResult scanSquarePs2(const ScanInput& input) {
  const auto bgms = findBgmLayouts(input.reader);
  const auto wds = findWdLayouts(input.reader);
  if (bgms.empty() && wds.empty()) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kSquarePs2FormatName), std::string(kSquarePs2FormatName));
  for (const auto& wd : wds) {
    if (!addWd(result, wd)) {
      result.warning("SquarePS2 WD header was recognized, but no playable instruments were found",
                     input.reader.range(wd.offset, wd.length));
    }
  }
  for (const auto& bgm : bgms) {
    const std::string name = fmt::format("{} BGM {}", result.sourceDisplayName(), bgm.sequenceId);
    auto sequence = result.sequence(name, input.reader.range(bgm.offset, bgm.length));
    sequence.data(SequenceData{.waveBankId = bgm.waveBankId})
        .program(parseBgm(input.reader, sequence.id(), bgm, &result.sourceMap(), &result.diagnostics()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{
      .name = std::string(kSquarePs2FormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .scan = scanSquarePs2,
      .resolveCollections = resolveCollections,
      .bindCollection = bindCollection,
  };
}

}  // namespace vgmtrans::formats::square_ps2

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AsciiShuichiSnes/AsciiShuichiSnes.h"

#include <fmt/format.h>

#include <string>

namespace vgmtrans::formats::ascii_shuichi_snes {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "AsciiShuichiSnes");
  const std::string displayName =
      fmt::format("{} (Shuichi Ukai, {})", result.sourceDisplayName(), versionName(layout->version));
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceSourceRange(input.reader, parsed.headerRange, parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.programs, displayName)) {
    collection.soundBank(*synth);
  } else {
    result.warning("AsciiShuichiSnes sequence found, but no valid referenced instruments were discovered",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{
      .name = "AsciiShuichiSnes",
      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
      .acceptedFormats = {source_formats::kSnesAram},
      .scan = scan,
  };
}

}  // namespace vgmtrans::formats::ascii_shuichi_snes

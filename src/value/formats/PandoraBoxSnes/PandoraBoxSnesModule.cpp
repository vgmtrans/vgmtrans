/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PandoraBoxSnes/PandoraBoxSnes.h"

#include <fmt/format.h>

#include <string>
#include <utility>

namespace vgmtrans::formats::pandora_box_snes {

using namespace core;

namespace {

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, kFormatName);
  const std::string displayName = fmt::format("{} ({})", result.sourceDisplayName(), versionName(layout->version));
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  const SourceRange header = input.reader.range(layout->sequenceHeaderAddress, kSequenceHeaderSize);
  sequence.range(sequenceSourceRange(input.reader, header, parsed.program))
      .program(std::move(parsed.program));

  auto collection = result.sourceCollection(displayName).sequence(sequence);
  if (const auto synth = addSynth(result, *layout, parsed.programs, displayName)) {
    collection.soundBank(*synth);
  } else {
    result.warning("PandoraBoxSnes sequence found, but no valid referenced instruments or BRR samples were found",
                   input.reader.range(0, input.reader.size()));
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{
      .name = kFormatName,
      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
      .acceptedFormats = {source_formats::kSnesAram},
      .scan = scan,
  };
}

}  // namespace vgmtrans::formats::pandora_box_snes

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TriAcePS1/TriAcePS1.h"

#include <fmt/format.h>

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::triace_ps1 {

using namespace core;

namespace {

[[nodiscard]] std::vector<TriAcePs1SequenceLayout> sequenceLayouts(const ScanInput& input) {
  if (input.source.knownFormat != kTriAcePs1ImageFormat) {
    return findTriAcePs1Sequences(input.reader);
  }
  std::vector<TriAcePs1SequenceLayout> layouts;
  for (const auto& segment : input.source.segments) {
    if (!segment.name.starts_with("sequence-") || segment.offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    if (auto layout = readTriAcePs1SequenceLayout(input.reader, static_cast<u32>(segment.offset));
        layout && layout->length <= segment.size) {
      layouts.push_back(std::move(*layout));
    }
  }
  return layouts;
}

[[nodiscard]] std::vector<TriAcePs1BankLayout> bankLayouts(const ScanInput& input) {
  if (const SourceSegment* ram = input.source.segment("ram"); ram != nullptr &&
                                                              ram->offset <= std::numeric_limits<u32>::max() &&
                                                              ram->size <= std::numeric_limits<u32>::max()) {
    return findTriAcePs1Banks(input.reader, static_cast<u32>(ram->offset), static_cast<u32>(ram->size));
  }
  return findTriAcePs1Banks(input.reader);
}

[[nodiscard]] ScanResult scanTriAcePs1(const ScanInput& input) {
  const auto sequences = sequenceLayouts(input);
  const auto banks = bankLayouts(input);
  if (sequences.empty() && banks.empty()) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kTriAcePs1FormatName));
  std::vector<ScanSoundBankDraft> bankDrafts;
  for (const auto& layout : banks) {
    if (auto bank = addTriAcePs1Bank(result, layout)) {
      bankDrafts.push_back(*bank);
    } else {
      result.warning("TriAcePS1 bank was recognized, but no playable SPU samples were found",
                     input.reader.range(layout.offset, layout.length));
    }
  }

  for (const auto& layout : sequences) {
    const std::string name = input.source.title.value_or(fmt::format("TriAcePS1 Sequence {:X}", layout.offset));
    auto sequence = result.sequence(name, input.reader.range(layout.offset, layout.length));
    sequence.program(
        parseTriAcePs1Sequence(input.reader, sequence.id(), layout, &result.sourceMap(), &result.diagnostics()));
    auto collection =
        result
            .collection(name,
                        CollectionKey{
                            .value = fmt::format("source:{}:sequence:{}", result.source().value, layout.offset),
                        })
            .sequence(sequence);
    for (const auto& bank : bankDrafts) {
      collection.soundBank(bank);
    }
  }
  return result.finish();
}

}  // namespace

FormatModule triAcePs1Module() {
  return FormatModule{
      .name = std::string(kTriAcePs1FormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .acceptedFormats = {std::string(kTriAcePs1ImageFormat), source_formats::kPlayStationRam},
      .scan = scanTriAcePs1,
  };
}

}  // namespace vgmtrans::formats::triace_ps1

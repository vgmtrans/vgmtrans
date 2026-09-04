/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

[[nodiscard]] bool rawVbSource(const SourceFile& source) {
  std::filesystem::path path = source.path.empty() ? std::filesystem::path(source.name) : source.path;
  std::string extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return extension == ".vb";
}

[[nodiscard]] std::vector<u16> bankNumbers(const std::vector<SonyPs1BankLayout>& layouts) {
  if (layouts.size() <= 1) {
    return std::vector<u16>(layouts.size(), 0);
  }
  std::map<u32, u32> occurrences;
  for (const auto& layout : layouts) {
    ++occurrences[layout.id];
  }
  std::vector<u16> numbers;
  numbers.reserve(layouts.size());
  for (u32 index = 0; index < layouts.size(); ++index) {
    const auto& layout = layouts[index];
    numbers.push_back(layout.id <= std::numeric_limits<u16>::max() && occurrences[layout.id] == 1
                          ? static_cast<u16>(layout.id)
                          : static_cast<u16>(index));
  }
  return numbers;
}

[[nodiscard]] ScanResult scanSonyPs1(const ScanInput& input) {
  const auto bankLayouts = findSonyPs1Banks(input.reader);
  const auto sequenceLayouts = findSonyPs1Sequences(input.reader);
  const bool rawBody = bankLayouts.empty() && sequenceLayouts.empty() && rawVbSource(input.source);
  if (bankLayouts.empty() && sequenceLayouts.empty() && !rawBody) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kSonyPs1FormatName), std::string(kSonyPs1CollectionResolver));
  if (rawBody) {
    if (!addSonyPs1RawSampleBody(result)) {
      return {};
    }
  }

  const auto numbers = bankNumbers(bankLayouts);
  for (u32 index = 0; index < bankLayouts.size(); ++index) {
    const auto& layout = bankLayouts[index];
    const u16 bank = numbers[index];
    addSonyPs1Bank(result, layout, bank);
  }

  for (const auto& layout : sequenceLayouts) {
    const std::string name = layout.sep
                                 ? fmt::format("{} SEP Sequence {}", result.sourceDisplayName(), layout.sequenceId)
                                 : fmt::format("{} SEQ {:X}", result.sourceDisplayName(), layout.offset);
    auto sequence = result.sequence(name, input.reader.range(layout.offset, layout.length));
    sequence.program(
        parseSonyPs1Sequence(input.reader, sequence.id(), layout, &result.sourceMap(), &result.diagnostics()));
  }
  return result.finish();
}

}  // namespace

FormatModule sonyPs1Module() {
  return FormatModule{
      .name = std::string(kSonyPs1FormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .acceptedFormats = {source_formats::kPlayStationRam},
      .scan = scanSonyPs1,
      .collectionResolverId = std::string(kSonyPs1CollectionResolver),
      .resolveCollections = resolveSonyPs1Collections,
      .bindCollection = bindSonyPs1Collection,
  };
}

}  // namespace vgmtrans::formats::sony_ps1

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TamsoftPS1/TamsoftPS1.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace vgmtrans::formats::tamsoft_ps1 {

using namespace core;

namespace {

[[nodiscard]] std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

[[nodiscard]] std::filesystem::path sourcePath(const SourceFile& source) {
  return source.path.empty() ? std::filesystem::path(source.name) : source.path;
}

[[nodiscard]] std::string sourceExtension(const SourceFile& source) {
  return lowercase(sourcePath(source).extension().string());
}

[[nodiscard]] std::string sourceStem(const SourceFile& source) {
  std::string stem = sourcePath(source).stem().string();
  if (stem.empty() && source.title) {
    stem = *source.title;
  }
  return stem;
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const std::string extension = sourceExtension(input.source);
  const bool sequenceCandidate = extension.empty() || extension == ".tsq";
  const bool bankCandidate = extension.empty() || extension == ".tvb" || extension == ".tvb2";
  const auto sequences = sequenceCandidate ? readSequenceLayouts(input.reader) : std::vector<SequenceLayout>{};
  const auto bank = bankCandidate ? readBankLayout(input.reader) : std::optional<BankLayout>{};
  if (sequences.empty() && !bank) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kFormatName));
  const std::string fileStem = sourceStem(input.source);
  const std::string stem = fileStem.empty() ? result.sourceDisplayName() : fileStem;
  // A TSQ is loaded as one driver bank. Mixed files such as C13BGM therefore
  // use BGM.TVB for every song entry, including embedded sound effects.
  const bool usesMusicBank = std::ranges::any_of(sequences, [](const SequenceLayout& layout) {
    return layout.type == 0;
  });
  if (bank && !addBank(result, *bank, stem)) {
    result.warning("Tamsoft TVB was recognized, but no playable instruments were found",
                   input.reader.range(0, kBankHeaderSize));
  }
  for (const auto& layout : sequences) {
    const std::string name = fmt::format("{} ({})", stem, layout.song);
    auto sequence = result.sequence(name, input.reader.range(0, input.reader.size()));
    sequence
        .program(parseSequence(input.reader, sequence.id(), layout, &result.sourceMap(), &result.diagnostics()))
        .data(SequenceData{
            .stem = stem,
            .song = layout.song,
            .generation = layout.generation,
            .usesMusicBank = usesMusicBank,
        });
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  return FormatModule{
      .name = std::string(kFormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .scan = scan,
      .resolveCollections = resolveCollections,
  };
}

}  // namespace vgmtrans::formats::tamsoft_ps1

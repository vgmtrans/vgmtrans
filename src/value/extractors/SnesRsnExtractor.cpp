/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/SnesRsnExtractor.h"

#include "unarr.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::snes_rsn {

using namespace core;

namespace {

constexpr std::array<u8, 7> kRarSignature{'R', 'a', 'r', '!', 0x1a, 0x07, 0x00};

[[nodiscard]] bool hasRarSignature(std::span<const u8> bytes) {
  return bytes.size() >= kRarSignature.size() &&
         std::ranges::equal(kRarSignature, bytes.subspan(0, kRarSignature.size()));
}

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

}  // namespace

[[nodiscard]] ExtractionResult extractSnesRsn(const ExtractionInput& input) {
  // RSN is a RAR archive convention for SPC sets. Extract every entry as a derived source
  // so the SPC extractor and SNES format scanners can process them normally.
  const auto bytes = input.reader.slice(0, input.reader.size());
  if (!hasRarSignature(bytes)) {
    return {};
  }

  ExtractionResult result;
  const auto sourceRange = input.reader.range(0, input.reader.size());

  std::unique_ptr<ar_stream, decltype(&ar_close)> stream(ar_open_memory(bytes.data(), bytes.size()), ar_close);
  if (stream == nullptr) {
    result.diagnostics.push_back(warning("RSN archive could not be opened", sourceRange));
    return result;
  }

  std::unique_ptr<ar_archive, decltype(&ar_close_archive)> archive(ar_open_rar_archive(stream.get()), ar_close_archive);
  if (archive == nullptr) {
    result.diagnostics.push_back(warning("RSN RAR archive could not be parsed", sourceRange));
    return result;
  }

  while (ar_parse_entry(archive.get())) {
    const char* rawName = ar_entry_get_name(archive.get());
    if (rawName == nullptr || rawName[0] == '\0') {
      result.diagnostics.push_back(warning("RSN archive entry had no name", sourceRange));
      continue;
    }

    const size_t entrySize = ar_entry_get_size(archive.get());
    std::vector<u8> entryBytes(entrySize);
    if (!ar_entry_uncompress(archive.get(), entryBytes.data(), entryBytes.size())) {
      result.diagnostics.push_back(
          warning("RSN archive entry could not be decompressed: " + std::string(rawName), sourceRange));
      continue;
    }

    result.sources.push_back(ExtractedSource{
        .file = SourceFile{.name = rawName, .path = input.source.path, .origin = sourceRange},
        .bytes = std::move(entryBytes),
    });
  }

  if (result.sources.empty() && result.diagnostics.empty()) {
    result.diagnostics.push_back(warning("RSN archive did not contain any extractable entries", sourceRange));
  }

  return result;
}

SourceExtractor snesRsnExtractor() {
  return SourceExtractor{
      .name = "SnesRsn",
      .acceptedFormats = {source_formats::kRsn},
      .extract = extractSnesRsn,
  };
}

}  // namespace vgmtrans::formats::snes_rsn

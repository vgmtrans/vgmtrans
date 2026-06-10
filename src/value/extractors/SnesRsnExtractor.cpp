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

struct StreamCloser {
  void operator()(ar_stream* stream) const noexcept {
    if (stream != nullptr) {
      ar_close(stream);
    }
  }
};

struct ArchiveCloser {
  void operator()(ar_archive* archive) const noexcept {
    if (archive != nullptr) {
      ar_close_archive(archive);
    }
  }
};

using StreamPtr = std::unique_ptr<ar_stream, StreamCloser>;
using ArchivePtr = std::unique_ptr<ar_archive, ArchiveCloser>;

[[nodiscard]] bool hasRarSignature(std::span<const u8> bytes) {
  return bytes.size() >= kRarSignature.size() &&
         std::ranges::equal(kRarSignature, bytes.subspan(0, kRarSignature.size()));
}

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

}  // namespace

std::string_view SnesRsnExtractor::name() const {
  return "SnesRsn";
}

bool SnesRsnExtractor::canScan(const SourceFile&, std::span<const u8> bytes) const {
  return hasRarSignature(bytes);
}

ScanResult SnesRsnExtractor::scan(const ScanInput& input) const {
  ScanResult result;
  const auto bytes = input.reader.slice(0, input.reader.size());
  const auto sourceRange = input.reader.range(0, input.reader.size());

  StreamPtr stream(ar_open_memory(bytes.data(), bytes.size()));
  if (stream == nullptr) {
    result.diagnostics.push_back(warning("RSN archive could not be opened", sourceRange));
    return result;
  }

  ArchivePtr archive(ar_open_rar_archive(stream.get()));
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
      result.diagnostics.push_back(warning("RSN archive entry could not be decompressed: " + std::string(rawName),
                                           sourceRange));
      continue;
    }

    result.extractedSources.push_back(ExtractedSource{
        .file = SourceFile{.name = rawName, .path = input.source.path},
        .bytes = std::move(entryBytes),
        .origin = sourceRange,
    });
  }

  if (result.extractedSources.empty() && result.diagnostics.empty()) {
    result.diagnostics.push_back(warning("RSN archive did not contain any extractable entries", sourceRange));
  }

  return result;
}

void registerSnesRsnExtractor(FormatRegistry& registry) {
  registry.add(std::make_unique<SnesRsnExtractor>());
}

}  // namespace vgmtrans::formats::snes_rsn

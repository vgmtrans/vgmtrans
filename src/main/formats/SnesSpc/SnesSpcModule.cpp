/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/SnesSpc/SnesSpcModule.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::snes_spc {

using namespace core;

namespace {

constexpr u64 kSpcRamOffset = 0x100;
constexpr u64 kSpcRamSize = 0x10000;
constexpr u64 kMinimumSpcSize = 0x10180;
constexpr std::string_view kSpcSignature = "SNES-SPC700 Sound File Data";

[[nodiscard]] bool hasSpcSignature(std::span<const u8> bytes) {
  if (bytes.size() < kMinimumSpcSize || bytes.size() < kSpcSignature.size()) {
    return false;
  }
  return std::ranges::equal(kSpcSignature, bytes.subspan(0, kSpcSignature.size())) &&
         bytes[0x21] == 0x1a && bytes[0x22] == 0x1a;
}

[[nodiscard]] std::string sourceName(const SourceFile& source) {
  if (!source.name.empty()) {
    return source.name;
  }
  if (!source.path.empty()) {
    return source.path.filename().string();
  }
  return "SPC";
}

}  // namespace

std::string_view SnesSpcModule::name() const {
  return "SnesSpc";
}

bool SnesSpcModule::canScan(const SourceFile&, std::span<const u8> bytes) const {
  return hasSpcSignature(bytes);
}

ScanResult SnesSpcModule::scan(const ScanInput& input) const {
  const auto ramBytes = input.reader.slice(kSpcRamOffset, kSpcRamSize);
  std::vector<u8> ram(ramBytes.begin(), ramBytes.end());
  const auto origin = input.reader.range(kSpcRamOffset, kSpcRamSize);

  ScanResult result;
  result.extractedSources.push_back(ExtractedSource{
      .file =
          SourceFile{
              .name = sourceName(input.source) + " - ram",
              .path = input.source.path,
          },
      .bytes = std::move(ram),
      .origin = origin,
  });
  return result;
}

void registerSnesSpcModule(FormatRegistry& registry) {
  registry.add(std::make_unique<SnesSpcModule>());
}

}  // namespace vgmtrans::formats::snes_spc

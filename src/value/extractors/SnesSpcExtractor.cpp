/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/SnesSpcExtractor.h"

#include "value/scan/FormatRegistry.h"

#include <algorithm>
#include <filesystem>
#include <optional>
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
constexpr u64 kId666TitleOffset = 0x2e;
constexpr u64 kId666TitleSize = 32;
constexpr u64 kExtendedId666Offset = 0x10200;
constexpr std::string_view kSpcSignature = "SNES-SPC700 Sound File Data";
constexpr std::string_view kExtendedId666Signature = "xid6";

[[nodiscard]] bool hasSpcSignature(std::span<const u8> bytes) {
  if (bytes.size() < kMinimumSpcSize || bytes.size() < kSpcSignature.size()) {
    return false;
  }
  return std::ranges::equal(kSpcSignature, bytes.subspan(0, kSpcSignature.size())) && bytes[0x21] == 0x1a &&
         bytes[0x22] == 0x1a;
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

[[nodiscard]] std::string nullTerminatedString(std::span<const u8> bytes, u64 offset, u64 maxLength) {
  if (offset >= bytes.size()) {
    return {};
  }

  const u64 available = std::min<u64>(maxLength, bytes.size() - offset);
  const auto field = bytes.subspan(offset, available);
  const auto end = std::ranges::find(field, u8{0});
  return std::string(reinterpret_cast<const char*>(field.data()), static_cast<size_t>(end - field.begin()));
}

[[nodiscard]] bool matches(std::span<const u8> bytes, u64 offset, std::string_view signature) {
  if (offset > bytes.size() || signature.size() > bytes.size() - offset) {
    return false;
  }
  const auto field = bytes.subspan(offset, signature.size());
  return std::equal(signature.begin(), signature.end(), field.begin(), field.end());
}

[[nodiscard]] u16 le16(std::span<const u8> bytes, u64 offset) {
  return static_cast<u16>(bytes[offset] | (bytes[offset + 1] << 8));
}

[[nodiscard]] u32 le32(std::span<const u8> bytes, u64 offset) {
  return static_cast<u32>(bytes[offset]) | (static_cast<u32>(bytes[offset + 1]) << 8) |
         (static_cast<u32>(bytes[offset + 2]) << 16) | (static_cast<u32>(bytes[offset + 3]) << 24);
}

[[nodiscard]] std::optional<std::string> spcTitle(std::span<const u8> bytes) {
  // Prefer extended ID666 titles when present, but fall back to the fixed SPC title field.
  std::string title;
  if (bytes.size() > 0x23 && bytes[0x23] == 0x1a) {
    title = nullTerminatedString(bytes, kId666TitleOffset, kId666TitleSize);
  }

  if (matches(bytes, kExtendedId666Offset, kExtendedId666Signature) && bytes.size() >= kExtendedId666Offset + 8) {
    const u32 chunkSize = le32(bytes, kExtendedId666Offset + 4);
    const u64 chunkBegin = kExtendedId666Offset + 8;
    const u64 remaining = bytes.size() - chunkBegin;
    const u64 chunkEnd = chunkBegin + std::min<u64>(remaining, chunkSize);

    for (u64 offset = chunkBegin; offset + 4 <= chunkEnd;) {
      const u8 id = bytes[offset];
      const u8 type = bytes[offset + 1];
      const u16 headerData = le16(bytes, offset + 2);

      if (type == 1) {
        const u64 dataBegin = offset + 4;
        const u64 dataEnd = dataBegin + headerData;
        if (dataEnd > chunkEnd) {
          break;
        }
        if (id == 0x01) {
          title = nullTerminatedString(bytes, dataBegin, headerData);
        }
        offset = dataBegin + ((static_cast<u64>(headerData) + 3) & ~u64{3});
      } else if (type == 4) {
        offset += 8;
      } else {
        offset += 4;
      }
    }
  }

  if (title.empty()) {
    return std::nullopt;
  }
  return title;
}

}  // namespace

[[nodiscard]] bool canScanSnesSpc(const SourceFile&, std::span<const u8> bytes) {
  return hasSpcSignature(bytes);
}

[[nodiscard]] ScanResult scanSnesSpc(const ScanInput& input) {
  // SPC files are snapshots. The actual format scanners want the 64 KiB ARAM image, so
  // expose RAM as a virtual child source and let normal SNES modules scan that.
  const auto spcBytes = input.reader.slice(0, input.reader.size());
  const auto ramBytes = input.reader.slice(kSpcRamOffset, kSpcRamSize);
  std::vector<u8> ram(ramBytes.begin(), ramBytes.end());
  const auto origin = input.reader.range(kSpcRamOffset, kSpcRamSize);

  ScanResult result;
  result.extractedSources.push_back(ExtractedSource{
      .file =
          SourceFile{
              .name = sourceName(input.source) + " - ram",
              .title = spcTitle(spcBytes),
              .path = input.source.path,
          },
      .bytes = std::move(ram),
      .origin = origin,
  });
  return result;
}

void registerSnesSpcExtractor(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = "SnesSpc",
      .canScan = canScanSnesSpc,
      .scan = scanSnesSpc,
  });
}

}  // namespace vgmtrans::formats::snes_spc

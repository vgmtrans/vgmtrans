/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/SnesSpcExtractor.h"
#include "value/scan/BytePattern.h"

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

[[nodiscard]] std::optional<std::string> spcTitle(ByteReader reader) {
  const auto bytes = reader.slice(0, reader.size());
  // Prefer extended ID666 titles when present, but fall back to the fixed SPC title field.
  std::string title;
  if (bytes.size() > 0x23 && bytes[0x23] == 0x1a) {
    title = nullTerminatedString(bytes, kId666TitleOffset, kId666TitleSize);
  }

  if (reader.has(kExtendedId666Offset, 8) && matchesBytes(reader, kExtendedId666Offset, kExtendedId666Signature)) {
    const u32 chunkSize = reader.le32(kExtendedId666Offset + 4);
    const u64 chunkBegin = kExtendedId666Offset + 8;
    const u64 remaining = bytes.size() - chunkBegin;
    const u64 chunkEnd = chunkBegin + std::min<u64>(remaining, chunkSize);

    for (u64 offset = chunkBegin; offset + 4 <= chunkEnd;) {
      const u8 id = bytes[offset];
      const u8 type = bytes[offset + 1];
      const u16 headerData = reader.le16(offset + 2);

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

[[nodiscard]] ExtractionResult extractSnesSpc(const ExtractionInput& input) {
  // SPC files are snapshots. The actual format scanners want the 64 KiB ARAM image, so
  // expose RAM as a derived child source and let normal SNES modules scan that.
  const auto reader = input.reader;
  if (reader.size() < kMinimumSpcSize || !matchesBytes(reader, 0, kSpcSignature) || reader.u8At(0x21) != 0x1a ||
      reader.u8At(0x22) != 0x1a) {
    return {};
  }

  const auto ramBytes = input.reader.slice(kSpcRamOffset, kSpcRamSize);
  std::vector<u8> ram(ramBytes.begin(), ramBytes.end());
  const auto origin = input.reader.range(kSpcRamOffset, kSpcRamSize);

  ExtractionResult result;
  result.sources.push_back(ExtractedSource{
      .file =
          SourceFile{
              .name = sourceName(input.source) + " - ram",
              .title = spcTitle(reader),
              .path = input.source.path,
              .origin = origin,
              .knownFormat = std::string(source_formats::kSnesAram),
          },
      .bytes = std::move(ram),
  });
  return result;
}

SourceExtractor snesSpcExtractor() {
  return SourceExtractor{
      .name = "SnesSpc",
      .acceptedFormats = {source_formats::kSpc},
      .extract = extractSnesSpc,
  };
}

}  // namespace vgmtrans::formats::snes_spc

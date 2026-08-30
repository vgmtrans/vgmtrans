/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TriAcePS1/TriAcePS1.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::triace_ps1 {

using namespace core;

namespace {

constexpr u32 kSlzHeaderSize = 0x10;
constexpr u32 kMaximumSequenceSize = 0x30000;
constexpr u32 kMaximumImplicitTail = 16;

[[nodiscard]] bool signatureAt(ByteReader reader, u64 offset) {
  return reader.has(offset, 4) && reader.u8At(offset) == 'S' && reader.u8At(offset + 1) == 'L' &&
         reader.u8At(offset + 2) == 'Z' && reader.u8At(offset + 3) <= 3;
}

[[nodiscard]] std::optional<std::vector<u8>> decompressSlz(ByteReader reader, u32 offset) {
  if (!signatureAt(reader, offset) || !reader.has(offset, kSlzHeaderSize)) {
    return std::nullopt;
  }
  const u8 mode = reader.u8At(offset + 3);
  const u32 declaredSize = reader.le32(offset + 8);
  if (declaredSize > kMaximumSequenceSize) {
    return std::nullopt;
  }
  const u32 outputLimit = declaredSize == 0 ? kMaximumSequenceSize : declaredSize;
  const u32 blockSize = reader.le32(offset + 12);
  const u64 compressedEnd = blockSize >= kSlzHeaderSize && blockSize <= reader.size() - offset
                                ? static_cast<u64>(offset) + blockSize
                                : reader.size();
  u64 cursor = static_cast<u64>(offset) + kSlzHeaderSize;
  std::vector<u8> output;
  output.reserve(outputLimit);

  const auto hasImplicitTail = [&] {
    return declaredSize != 0 && output.size() >= 4 && output[0] == 0xff && output[1] == 0xff &&
           static_cast<u32>(output[2] | (static_cast<u16>(output[3]) << 8)) + 2 == declaredSize &&
           output.size() < declaredSize && declaredSize - output.size() <= kMaximumImplicitTail;
  };

  auto append = [&](u8 value) {
    if (output.size() >= outputLimit) {
      return false;
    }
    output.push_back(value);
    return true;
  };
  if (mode == 0) {
    if (cursor > compressedEnd || outputLimit > compressedEnd - cursor) {
      return std::nullopt;
    }
    const auto bytes = reader.slice(cursor, outputLimit);
    output.assign(bytes.begin(), bytes.end());
    return output;
  }

  const u32 flagBits = mode == 3 ? 16 : 8;
  bool terminated = false;
  while (output.size() < outputLimit && !terminated) {
    if (cursor + flagBits / 8 > compressedEnd) {
      return std::nullopt;
    }
    u16 flags = flagBits == 16 ? reader.le16(cursor) : reader.u8At(cursor);
    cursor += flagBits / 8;
    for (u32 bit = 0; bit < flagBits && output.size() < outputLimit; ++bit, flags >>= 1) {
      if ((flags & 1) != 0) {
        const u32 literalBytes = mode == 3 ? 2 : 1;
        if (cursor + literalBytes > compressedEnd) {
          return std::nullopt;
        }
        for (u32 i = 0; i < literalBytes; ++i) {
          if (!append(reader.u8At(cursor++))) {
            break;
          }
        }
        continue;
      }

      if (cursor + 2 > compressedEnd) {
        return std::nullopt;
      }
      u8 first = reader.u8At(cursor++);
      const u8 second = reader.u8At(cursor++);
      if (first == 0 && second == 0) {
        terminated = true;
        break;
      }

      u32 count = 0;
      if (mode == 2 && second >= 0xf0) {
        if (second == 0xf0) {
          count = static_cast<u32>(first) + 0x13;
          if (cursor >= compressedEnd) {
            return std::nullopt;
          }
          first = reader.u8At(cursor++);
        } else {
          count = (second & 0x0f) + 3;
        }
        while (count-- != 0 && append(first)) {
        }
        continue;
      }

      const u32 distance = ((second & 0x0f) << 8) | first;
      count = (second >> 4) + 3;
      // tri-Ace uses a zero-distance match as compact zero padding. It occurs
      // at the tail of short Star Ocean streams; treating it as a normal
      // back-reference would read the byte currently being written.
      if (distance == 0) {
        while (count-- != 0 && append(0)) {
        }
        continue;
      }
      if (distance > output.size()) {
        // A few Star Ocean streams stop after the last pattern-end byte and
        // leave their declared alignment tail unwritten. Other streams encode
        // that same, unreferenced tail as zeroes; an ensuing token is garbage.
        if (!hasImplicitTail()) {
          return std::nullopt;
        }
        terminated = true;
        break;
      }
      u32 source = static_cast<u32>(output.size()) - distance;
      while (count-- != 0) {
        if (!append(output[source++])) {
          break;
        }
      }
    }
  }
  if (hasImplicitTail()) {
    output.resize(declaredSize, 0);
  }
  if ((declaredSize != 0 && output.size() != declaredSize) || output.empty()) {
    return std::nullopt;
  }
  return output;
}

[[nodiscard]] std::string sourceName(const SourceFile& source) {
  if (!source.name.empty()) {
    return source.name;
  }
  if (!source.path.empty()) {
    return source.path.filename().string();
  }
  return "PlayStation RAM";
}

[[nodiscard]] ExtractionResult extractTriAcePs1(const ExtractionInput& input) {
  std::vector<std::vector<u8>> sequences;
  for (u64 offset = 0; offset + kSlzHeaderSize <= input.reader.size(); ++offset) {
    if (!signatureAt(input.reader, offset) || offset > std::numeric_limits<u32>::max()) {
      continue;
    }
    auto bytes = decompressSlz(input.reader, static_cast<u32>(offset));
    if (!bytes) {
      continue;
    }
    ByteReader decompressed({}, std::span<const u8>(*bytes));
    if (readTriAcePs1SequenceLayout(decompressed, 0)) {
      sequences.push_back(std::move(*bytes));
    }
  }
  if (sequences.empty()) {
    return {};
  }

  const auto original = input.reader.slice(0, input.reader.size());
  std::vector<u8> bytes(original.begin(), original.end());
  SourceFile file{
      .name = sourceName(input.source) + " - TriAcePS1 image",
      .title = input.source.title,
      .path = input.source.path,
      .origin = input.reader.range(0, input.reader.size()),
      .knownFormat = std::string(kTriAcePs1ImageFormat),
  };
  file.segments.push_back(SourceSegment{.name = "ram", .offset = 0, .size = bytes.size()});
  for (u32 index = 0; index < sequences.size(); ++index) {
    const u64 offset = bytes.size();
    file.segments.push_back(SourceSegment{
        .name = "sequence-" + std::to_string(index),
        .offset = offset,
        .size = sequences[index].size(),
    });
    bytes.insert(bytes.end(), std::make_move_iterator(sequences[index].begin()),
                 std::make_move_iterator(sequences[index].end()));
  }

  ExtractionResult result;
  result.sources.push_back(ExtractedSource{.file = std::move(file), .bytes = std::move(bytes)});
  return result;
}

}  // namespace

SourceExtractor triAcePs1Extractor() {
  return SourceExtractor{
      .name = std::string(kTriAcePs1FormatName),
      .acceptedFormats = {source_formats::kPlayStationRam},
      .extract = extractTriAcePs1,
  };
}

}  // namespace vgmtrans::formats::triace_ps1

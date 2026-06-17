/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ParseCursor.h"

#include <limits>
#include <string>

namespace vgmtrans::core {

ParseCursor::ParseCursor(ByteReader reader, SourceRange bounds, std::vector<Diagnostic>& diagnostics)
    : reader_(reader), bounds_(bounds), diagnostics_(diagnostics) {
}

std::optional<SourceRange> ParseCursor::range(u64 relativeOffset, u64 size, std::string_view field) {
  const auto absolute = absoluteOffset(relativeOffset, size, field);
  if (!absolute) {
    return std::nullopt;
  }
  return reader_.range(*absolute, size);
}

std::optional<u8> ParseCursor::u8(u64 relativeOffset, std::string_view field) {
  const auto absolute = absoluteOffset(relativeOffset, 1, field);
  return absolute ? std::optional<::u8>{reader_.u8At(*absolute)} : std::nullopt;
}

std::optional<u16> ParseCursor::le16(u64 relativeOffset, std::string_view field) {
  const auto absolute = absoluteOffset(relativeOffset, 2, field);
  return absolute ? std::optional<u16>{reader_.le16(*absolute)} : std::nullopt;
}

std::optional<u32> ParseCursor::le32(u64 relativeOffset, std::string_view field) {
  const auto absolute = absoluteOffset(relativeOffset, 4, field);
  return absolute ? std::optional<u32>{reader_.le32(*absolute)} : std::nullopt;
}

std::optional<u64> ParseCursor::absoluteOffset(u64 relativeOffset, u64 size, std::string_view field) {
  if (!bounds_.valid() || bounds_.source != reader_.source()) {
    report(field, "cursor range does not refer to this source", bounds_);
    return std::nullopt;
  }
  if (relativeOffset > bounds_.size || size > bounds_.size - relativeOffset) {
    u64 diagnosticOffset = bounds_.offset;
    if (bounds_.offset <= std::numeric_limits<u64>::max() - relativeOffset) {
      diagnosticOffset = bounds_.offset + relativeOffset;
    }
    report(field, "field is outside the parser range",
           SourceRange{.source = bounds_.source, .offset = diagnosticOffset});
    return std::nullopt;
  }
  if (bounds_.offset > std::numeric_limits<u64>::max() - relativeOffset) {
    report(field, "field offset overflowed", bounds_);
    return std::nullopt;
  }

  const u64 absolute = bounds_.offset + relativeOffset;
  if (!reader_.has(absolute, size)) {
    report(field, "field is outside the source bytes",
           SourceRange{.source = bounds_.source, .offset = absolute, .size = size});
    return std::nullopt;
  }
  return absolute;
}

void ParseCursor::report(std::string_view field, std::string_view detail, SourceRange range) {
  ok_ = false;
  diagnostics_.push_back(Diagnostic{
      .severity = Severity::Warning,
      .message = "Could not read " + std::string(field) + ": " + std::string(detail),
      .range = range,
  });
}

}  // namespace vgmtrans::core

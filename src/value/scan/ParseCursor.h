/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

// Small checked reader for scanner code. It keeps malformed-byte handling local:
// a failed read records a diagnostic and returns std::nullopt instead of throwing.
class ParseCursor {
public:
  ParseCursor(ByteReader reader, SourceRange bounds, std::vector<Diagnostic>& diagnostics);

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] std::optional<SourceRange> range(u64 relativeOffset, u64 size, std::string_view field);
  [[nodiscard]] RangedValue<::u8> u8(u64 relativeOffset, std::string_view field);
  [[nodiscard]] RangedValue<u16> le16(u64 relativeOffset, std::string_view field);
  [[nodiscard]] RangedValue<u32> le32(u64 relativeOffset, std::string_view field);

private:
  [[nodiscard]] std::optional<u64> absoluteOffset(u64 relativeOffset, u64 size, std::string_view field);
  void report(std::string_view field, std::string_view detail, SourceRange range);

  ByteReader reader_;
  SourceRange bounds_;
  std::vector<Diagnostic>& diagnostics_;
  bool ok_ = true;
};

}  // namespace vgmtrans::core

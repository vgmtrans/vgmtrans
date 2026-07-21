/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/SourceMap.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

// A small, source-aware reader for one structured record. It is deliberately
// not a schema language: format code reads fields in ordinary control flow,
// while RecordReader owns cursor bounds, exact field ranges, and truncation
// diagnostics.
class RecordReader {
public:
  RecordReader(ByteReader reader, u32 offset, u32 end, std::vector<Diagnostic>* diagnostics = nullptr);

  [[nodiscard]] RangedValue<::u8> u8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<::s8> s8(std::string_view name,
                                     SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  [[nodiscard]] RangedValue<u16> u16be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<u16> u16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<u32> u24le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<u32> u32be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<u32> u32le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<u32> varLen(std::string_view name,
                                        SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<std::string> rawBytes(std::string_view name, u32 size);
  [[nodiscard]] RangedValue<s16> s16be(std::string_view name,
                                       SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  [[nodiscard]] RangedValue<s16> s16le(std::string_view name,
                                       SourceValueDisplay display = SourceValueDisplay::SignedDecimal);

  // Fixed layouts are often clearest when their documented offsets remain
  // visible in code. These reads use offsets from the record's beginning while
  // still extending its range and collecting exact source fields.
  [[nodiscard]] RangedValue<::u8> u8At(u32 relativeOffset, std::string_view name,
                                       SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<::s8> s8At(u32 relativeOffset, std::string_view name,
                                       SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  [[nodiscard]] RangedValue<u16> u16beAt(u32 relativeOffset, std::string_view name,
                                         SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<u16> u16leAt(u32 relativeOffset, std::string_view name,
                                         SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<s16> s16beAt(u32 relativeOffset, std::string_view name,
                                         SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  [[nodiscard]] RangedValue<s16> s16leAt(u32 relativeOffset, std::string_view name,
                                         SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  [[nodiscard]] RangedValue<u32> u32beAt(u32 relativeOffset, std::string_view name,
                                         SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] RangedValue<u32> u32leAt(u32 relativeOffset, std::string_view name,
                                         SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] std::optional<::u8> peekU8() const;

  template <class T>
  void derived(std::string_view name, T&& value, SourceValueDisplay display = SourceValueDisplay::Default) {
    fields_.push_back(SourceField{
        .name = std::string(name),
        .value = makeSourceValue(std::forward<T>(value)),
        .display = display,
    });
  }

  [[nodiscard]] u32 begin() const noexcept { return begin_; }
  [[nodiscard]] u32 position() const noexcept { return position_; }
  [[nodiscard]] u32 size() const noexcept { return position_ - begin_; }
  [[nodiscard]] bool ok() const noexcept { return !failed_; }
  [[nodiscard]] SourceRange range() const noexcept { return reader_.range(begin_, size()); }
  [[nodiscard]] std::span<const ::u8> bytes() const;
  // The finished value covers the record bounds supplied to the constructor,
  // including reserved bytes that were intentionally not decoded as fields.
  [[nodiscard]] SourceRecord finish() && noexcept;

private:
  bool require(u32 size, std::string_view field);
  [[nodiscard]] std::optional<u32> requireAt(u32 relativeOffset, u32 size, std::string_view field);
  void field(std::string_view name, SourceRange range, SourceValue value, SourceValueDisplay display);

  ByteReader reader_;
  u32 begin_ = 0;
  u32 position_ = 0;
  u32 end_ = 0;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  std::vector<SourceField> fields_;
  bool failed_ = false;
};

}  // namespace vgmtrans::core

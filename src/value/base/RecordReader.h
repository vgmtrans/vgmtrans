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
// diagnostics. Reads record source fields even when the caller does not need
// their returned value; ok() and diagnostics still report incomplete records.
class RecordReader {
public:
  // Clamp the window to the source. Reversed or out-of-source windows are
  // empty, so every cursor and range stays within the source bounds.
  RecordReader(ByteReader reader, u32 offset, u32 end, std::vector<Diagnostic>* diagnostics = nullptr,
               bool captureFields = true);

  RangedValue<::u8> u8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<::s8> s8(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  RangedValue<u16> u16be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<u16> u16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<u32> u24le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<u32> u32be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<u32> u32le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<u32> varLen(std::string_view name, SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<std::string> rawBytes(std::string_view name, u32 size);
  RangedValue<s16> s16be(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  RangedValue<s16> s16le(std::string_view name, SourceValueDisplay display = SourceValueDisplay::SignedDecimal);

  // Fixed layouts are often clearest when their documented offsets remain
  // visible in code. These reads use offsets from the record's beginning while
  // still extending its range and collecting exact source fields.
  RangedValue<::u8> u8At(u64 relativeOffset, std::string_view name,
                         SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<::s8> s8At(u64 relativeOffset, std::string_view name,
                         SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  RangedValue<u16> u16beAt(u64 relativeOffset, std::string_view name,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<u16> u16leAt(u64 relativeOffset, std::string_view name,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<s16> s16beAt(u64 relativeOffset, std::string_view name,
                           SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  RangedValue<s16> s16leAt(u64 relativeOffset, std::string_view name,
                           SourceValueDisplay display = SourceValueDisplay::SignedDecimal);
  RangedValue<u32> u32beAt(u64 relativeOffset, std::string_view name,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  RangedValue<u32> u32leAt(u64 relativeOffset, std::string_view name,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  std::optional<SourceRange> rangeAt(u64 relativeOffset, u64 size, std::string_view name);
  [[nodiscard]] std::optional<::u8> peekU8() const;

  template <class T>
  void derived(std::string_view name, T&& value, SourceValueDisplay display = SourceValueDisplay::Default) {
    if (!captureFields_) {
      return;
    }
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
  template <class T, auto Read>
  RangedValue<T> number(std::string_view name, SourceValueDisplay display);
  template <class T, auto Read>
  RangedValue<T> numberAt(u64 relativeOffset, std::string_view name, SourceValueDisplay display);

  bool require(u32 size, std::string_view field);
  [[nodiscard]] std::optional<u32> requireAt(u64 relativeOffset, u64 size, std::string_view field);
  void field(std::string_view name, SourceRange range, SourceValue value, SourceValueDisplay display);

  ByteReader reader_;
  u32 begin_ = 0;
  u32 position_ = 0;
  u32 end_ = 0;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  std::vector<SourceField> fields_;
  bool captureFields_ = true;
  bool failed_ = false;
};

}  // namespace vgmtrans::core

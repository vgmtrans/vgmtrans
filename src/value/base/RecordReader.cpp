/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/RecordReader.h"

#include <algorithm>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string hexBytes(std::span<const ::u8> bytes) {
  static constexpr char kDigits[] = "0123456789ABCDEF";

  std::string out;
  out.reserve(bytes.size() * 3);
  for (const ::u8 byte : bytes) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0x0f]);
  }
  return out;
}

}  // namespace

RecordReader::RecordReader(ByteReader reader, u32 offset, u32 end, std::vector<Diagnostic>* diagnostics)
    : reader_(reader), begin_(offset), position_(offset), end_(static_cast<u32>(std::min<u64>(end, reader.size()))),
      diagnostics_(diagnostics) {
}

RangedValue<::u8> RecordReader::u8(std::string_view name, SourceValueDisplay display) {
  if (!require(1, name)) {
    return {};
  }
  const SourceRange sourceRange = reader_.range(position_, 1);
  const auto value = reader_.u8At(position_++);
  field(name, sourceRange, makeSourceValue(value), display);
  return RangedValue<::u8>{value, sourceRange};
}

RangedValue<::s8> RecordReader::s8(std::string_view name, SourceValueDisplay display) {
  if (!require(1, name)) {
    return {};
  }
  const SourceRange sourceRange = reader_.range(position_, 1);
  const auto value = reader_.s8At(position_++);
  field(name, sourceRange, makeSourceValue(value), display);
  return RangedValue<::s8>{value, sourceRange};
}

RangedValue<u16> RecordReader::u16be(std::string_view name, SourceValueDisplay display) {
  if (!require(2, name)) {
    return {};
  }
  const SourceRange sourceRange = reader_.range(position_, 2);
  const auto value = reader_.be16(position_);
  position_ += 2;
  field(name, sourceRange, makeSourceValue(value), display);
  return RangedValue<u16>{value, sourceRange};
}

RangedValue<u16> RecordReader::u16le(std::string_view name, SourceValueDisplay display) {
  if (!require(2, name)) {
    return {};
  }
  const SourceRange sourceRange = reader_.range(position_, 2);
  const auto value = reader_.le16(position_);
  position_ += 2;
  field(name, sourceRange, makeSourceValue(value), display);
  return RangedValue<u16>{value, sourceRange};
}

RangedValue<u32> RecordReader::u24le(std::string_view name, SourceValueDisplay display) {
  if (!require(3, name)) {
    return {};
  }
  const SourceRange sourceRange = reader_.range(position_, 3);
  const u32 value = reader_.u8At(position_) | (reader_.u8At(position_ + 1) << 8) | (reader_.u8At(position_ + 2) << 16);
  position_ += 3;
  field(name, sourceRange, makeSourceValue(value), display);
  return RangedValue<u32>{value, sourceRange};
}

RangedValue<u32> RecordReader::varLen(std::string_view name, SourceValueDisplay display) {
  const u32 begin = position_;
  u32 value = 0;
  while (true) {
    if (!require(1, name)) {
      return {};
    }
    const ::u8 byte = reader_.u8At(position_++);
    value = (value << 7) + (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      break;
    }
  }

  const SourceRange sourceRange = reader_.range(begin, position_ - begin);
  field(name, sourceRange, makeSourceValue(value), display);
  return RangedValue<u32>{value, sourceRange};
}

RangedValue<std::string> RecordReader::rawBytes(std::string_view name, u32 size) {
  const u32 begin = position_;
  const u32 available = begin <= end_ ? std::min(size, end_ - begin) : 0;
  const std::string value = hexBytes(reader_.slice(begin, available));
  position_ += available;
  if (available != 0) {
    field(name, reader_.range(begin, available), makeSourceValue(value), SourceValueDisplay::Hex);
  }
  if (available != size) {
    static_cast<void>(require(size - available, name));
  }
  return available != 0 ? RangedValue<std::string>{value, reader_.range(begin, available)} : RangedValue<std::string>{};
}

RangedValue<s16> RecordReader::s16be(std::string_view name, SourceValueDisplay display) {
  if (!require(2, name)) {
    return {};
  }
  const SourceRange sourceRange = reader_.range(position_, 2);
  const auto value = static_cast<s16>(reader_.be16(position_));
  position_ += 2;
  field(name, sourceRange, makeSourceValue(value), display);
  return RangedValue<s16>{value, sourceRange};
}

void RecordReader::addFields(AnnotationBuilder annotation) const {
  for (const auto& sourceField : fields_) {
    if (sourceField.range.valid()) {
      annotation.field(sourceField.name, sourceField.range, sourceField.value, sourceField.display);
    } else {
      annotation.derived(sourceField.name, sourceField.value, sourceField.display);
    }
  }
}

std::span<const ::u8> RecordReader::bytes() const {
  return reader_.slice(begin_, size());
}

std::vector<SourceField> RecordReader::takeFields() noexcept {
  return std::move(fields_);
}

bool RecordReader::require(u32 size, std::string_view fieldName) {
  if (!failed_ && position_ <= end_ && size <= end_ - position_ && reader_.has(position_, size)) {
    return true;
  }

  const u32 fieldBegin = position_;
  const u32 available = !failed_ && position_ <= end_ ? std::min(size, end_ - position_) : 0;
  if (!failed_ && diagnostics_ != nullptr) {
    diagnostics_->push_back(Diagnostic{
        .severity = Severity::Warning,
        .code = "truncated-record",
        .message = "Truncated field '" + std::string(fieldName) + "'",
        .range = reader_.range(fieldBegin, available),
    });
  }
  position_ += available;
  failed_ = true;
  return false;
}

void RecordReader::field(std::string_view name, SourceRange range, SourceValue value, SourceValueDisplay display) {
  fields_.push_back(SourceField{
      .name = std::string(name),
      .range = range,
      .value = std::move(value),
      .display = display,
  });
}

}  // namespace vgmtrans::core

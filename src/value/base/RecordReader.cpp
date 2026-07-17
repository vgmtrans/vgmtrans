/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/RecordReader.h"

#include <algorithm>
#include <string>

namespace vgmtrans::core {

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

void RecordReader::addFields(AnnotationBuilder annotation) const {
  for (const auto& sourceField : fields_) {
    annotation.field(sourceField.name, sourceField.range, sourceField.value, sourceField.display);
  }
}

std::span<const ::u8> RecordReader::bytes() const {
  return reader_.slice(begin_, size());
}

bool RecordReader::require(u32 size, std::string_view fieldName) {
  if (!failed_ && position_ <= end_ && size <= end_ - position_ && reader_.has(position_, size)) {
    return true;
  }

  if (!failed_ && diagnostics_ != nullptr) {
    const u32 available = position_ <= end_ ? end_ - position_ : 0;
    diagnostics_->push_back(Diagnostic{
        .severity = Severity::Warning,
        .code = "truncated-record",
        .message = "Truncated field '" + std::string(fieldName) + "'",
        .range = reader_.range(position_, available),
    });
  }
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

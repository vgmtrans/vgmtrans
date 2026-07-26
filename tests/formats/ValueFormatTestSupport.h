/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/SequenceModulationProfile.h"

#include "value/model/SourceMap.h"
#include "value/sequence/SequenceProgram.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

inline const vgmtrans::core::SourceAnnotation& commandAnnotation(const vgmtrans::core::SourceMap& sourceMap,
                                                                 const vgmtrans::core::SourceCommand& command) {
  const auto* annotation = sourceMap.find(command.annotation);
  if (annotation == nullptr) {
    throw std::runtime_error("decoded command should have a source annotation at " +
                             std::to_string(command.range.offset));
  }
  return *annotation;
}

inline std::string_view commandDetailKind(const vgmtrans::core::SourceMap& sourceMap,
                                          const vgmtrans::core::SourceCommand& command) {
  return commandAnnotation(sourceMap, command).detailKind;
}

inline const vgmtrans::core::SourceField* fieldWithName(const vgmtrans::core::SourceAnnotation& annotation,
                                                        std::string_view name) {
  const auto found = std::ranges::find_if(
      annotation.fields, [name](const vgmtrans::core::SourceField& field) { return field.name == name; });
  return found == annotation.fields.end() ? nullptr : &*found;
}

inline bool fieldEquals(const vgmtrans::core::SourceField* field, u64 value) {
  const auto* stored = field != nullptr ? std::get_if<u64>(&field->value) : nullptr;
  return stored != nullptr && *stored == value;
}

inline bool fieldEquals(const vgmtrans::core::SourceField* field, s64 value) {
  const auto* stored = field != nullptr ? std::get_if<s64>(&field->value) : nullptr;
  return stored != nullptr && *stored == value;
}

inline bool fieldEquals(const vgmtrans::core::SourceField* field, double value) {
  const auto* stored = field != nullptr ? std::get_if<double>(&field->value) : nullptr;
  return stored != nullptr && *stored == value;
}

inline bool fieldEquals(const vgmtrans::core::SourceField* field, std::string_view value) {
  const auto* stored = field != nullptr ? std::get_if<std::string>(&field->value) : nullptr;
  return stored != nullptr && *stored == value;
}

inline bool hasCommandAnnotation(const vgmtrans::core::SourceMap& sourceMap, vgmtrans::core::SourceId source,
                                 std::string_view detailKind, u32 offset) {
  const auto commandIds = sourceMap.withRole(source, vgmtrans::core::SourceRole::Command);
  return std::ranges::any_of(commandIds, [&](vgmtrans::core::SourceAnnotationId id) {
    const vgmtrans::core::SourceAnnotation& annotation = sourceMap.get(id);
    return annotation.range.offset == offset && annotation.detailKind == detailKind;
  });
}

inline const vgmtrans::core::SourceAnnotation& commandAnnotationAt(const vgmtrans::core::SourceMap& sourceMap,
                                                                   vgmtrans::core::SourceId source, u32 offset) {
  const auto commandIds = sourceMap.withRole(source, vgmtrans::core::SourceRole::Command);
  const auto found = std::ranges::find_if(commandIds, [&](vgmtrans::core::SourceAnnotationId id) {
    return sourceMap.get(id).range.offset == offset;
  });
  if (found == commandIds.end()) {
    throw std::runtime_error("expected command annotation was not found");
  }
  return sourceMap.get(*found);
}

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

[[nodiscard]] inline std::optional<SourceRange> validDiagnosticRange(SourceRange range) {
  if (!range.valid()) {
    return std::nullopt;
  }
  return range;
}

[[nodiscard]] inline Diagnostic exportError(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
      .range = range,
  };
}

[[nodiscard]] inline Diagnostic exportWarning(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

}  // namespace vgmtrans::core

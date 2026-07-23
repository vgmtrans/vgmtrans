/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/ValidationReport.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

bool ValidationReport::hasErrors() const noexcept {
  return std::ranges::any_of(diagnostics_,
                             [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; });
}

void ValidationReport::merge(ValidationReport report) {
  diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(report.diagnostics_.begin()),
                      std::make_move_iterator(report.diagnostics_.end()));
}

void ValidationReport::error(std::string code, std::string message, std::optional<SourceRange> range) {
  diagnostics_.push_back(Diagnostic{
      .severity = Severity::Error,
      .code = std::move(code),
      .message = std::move(message),
      .range = range,
  });
}

void ValidationReport::throwIfErrors(std::string_view prefix) const {
  const auto found = std::ranges::find_if(
      diagnostics_, [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; });
  if (found == diagnostics_.end()) {
    return;
  }

  if (prefix.empty()) {
    throw std::invalid_argument(found->message);
  }
  throw std::invalid_argument(std::string(prefix) + ": " + found->message);
}

}  // namespace vgmtrans::core

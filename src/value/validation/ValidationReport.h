/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// Validators accumulate the same diagnostics consumed by Session and the UI.
class ValidationReport {
public:
  [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept { return diagnostics_; }
  [[nodiscard]] std::vector<Diagnostic> takeDiagnostics() noexcept { return std::move(diagnostics_); }
  [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }

  void merge(ValidationReport report) {
    diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(report.diagnostics_.begin()),
                        std::make_move_iterator(report.diagnostics_.end()));
  }

  void error(std::string code, std::string message, std::optional<SourceRange> range = std::nullopt) {
    diagnostics_.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = std::move(code),
        .message = std::move(message),
        .range = range,
    });
  }

private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace vgmtrans::core

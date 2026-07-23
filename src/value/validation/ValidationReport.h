/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

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

  void merge(ValidationReport report);
  void error(std::string code, std::string message, std::optional<SourceRange> range = std::nullopt);

private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace vgmtrans::core

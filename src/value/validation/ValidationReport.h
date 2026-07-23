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
#include <string_view>
#include <vector>

namespace vgmtrans::core {

// Validators accumulate the same diagnostics consumed by Session and the UI.
// The report adds only composition and the admission-time throwing policy.
class ValidationReport {
public:
  [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept { return diagnostics_; }
  [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }
  [[nodiscard]] bool hasErrors() const noexcept;

  void merge(ValidationReport report);
  void error(std::string code, std::string message, std::optional<SourceRange> range = std::nullopt);
  void throwIfErrors(std::string_view prefix = {}) const;

private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace vgmtrans::core

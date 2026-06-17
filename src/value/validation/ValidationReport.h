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

// One validation problem tied back to the value object that caused it when possible.
// Validators use this richer shape; callers can still convert findings to plain diagnostics.
struct ValidationFinding {
  Severity severity = Severity::Info;
  std::string code;
  std::string message;
  std::optional<SourceRange> range;
  std::optional<SourceId> source;
  std::optional<AssetId> asset;
  std::optional<CollectionId> collection;
};

class ValidationReport {
public:
  [[nodiscard]] std::span<const ValidationFinding> findings() const noexcept { return findings_; }
  [[nodiscard]] bool empty() const noexcept { return findings_.empty(); }
  [[nodiscard]] bool hasErrors() const noexcept;
  [[nodiscard]] std::vector<Diagnostic> diagnostics() const;

  void add(ValidationFinding finding);
  void merge(ValidationReport report);
  void error(std::string code, std::string message, std::optional<SourceRange> range = std::nullopt);
  void warning(std::string code, std::string message, std::optional<SourceRange> range = std::nullopt);
  void throwIfErrors(std::string_view prefix = {}) const;

private:
  std::vector<ValidationFinding> findings_;
};

}  // namespace vgmtrans::core

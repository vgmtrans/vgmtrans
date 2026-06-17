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
  return std::ranges::any_of(findings_,
                             [](const ValidationFinding& finding) { return finding.severity == Severity::Error; });
}

std::vector<Diagnostic> ValidationReport::diagnostics() const {
  std::vector<Diagnostic> diagnostics;
  diagnostics.reserve(findings_.size());
  for (const auto& finding : findings_) {
    diagnostics.push_back(Diagnostic{
        .severity = finding.severity,
        .message = finding.message,
        .range = finding.range,
    });
  }
  return diagnostics;
}

void ValidationReport::add(ValidationFinding finding) {
  findings_.push_back(std::move(finding));
}

void ValidationReport::merge(ValidationReport report) {
  auto findings = std::move(report.findings_);
  findings_.insert(findings_.end(), std::make_move_iterator(findings.begin()), std::make_move_iterator(findings.end()));
}

void ValidationReport::error(std::string code, std::string message, std::optional<SourceRange> range) {
  add(ValidationFinding{
      .severity = Severity::Error,
      .code = std::move(code),
      .message = std::move(message),
      .range = range,
  });
}

void ValidationReport::warning(std::string code, std::string message, std::optional<SourceRange> range) {
  add(ValidationFinding{
      .severity = Severity::Warning,
      .code = std::move(code),
      .message = std::move(message),
      .range = range,
  });
}

void ValidationReport::throwIfErrors(std::string_view prefix) const {
  const auto found = std::ranges::find_if(
      findings_, [](const ValidationFinding& finding) { return finding.severity == Severity::Error; });
  if (found == findings_.end()) {
    return;
  }

  if (prefix.empty()) {
    throw std::invalid_argument(found->message);
  }
  throw std::invalid_argument(std::string(prefix) + ": " + found->message);
}

}  // namespace vgmtrans::core

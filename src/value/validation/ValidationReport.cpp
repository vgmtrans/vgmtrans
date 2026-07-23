/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/ValidationReport.h"

#include <iterator>
#include <utility>

namespace vgmtrans::core {

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

}  // namespace vgmtrans::core

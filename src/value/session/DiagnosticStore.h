/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

// Owns diagnostics produced while a Session is loaded. Keeping source cleanup here
// makes source removal simple: remove the source family, then discard every warning
// or error that pointed into those bytes.
class DiagnosticStore {
public:
  [[nodiscard]] const std::vector<Diagnostic>& all() const noexcept { return diagnostics_; }

  void addError(std::string message, std::optional<SourceRange> range = std::nullopt);
  void append(std::vector<Diagnostic> diagnostics);
  void removeForSources(const std::vector<SourceId>& sources);

private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "../TestSupport.h"

#include "value/base/CoreTypes.h"

#include <algorithm>
#include <vector>

using namespace vgmtrans::core;

namespace {

const Diagnostic& diagnosticWithMessage(const std::vector<Diagnostic>& diagnostics, std::string_view message) {
  const auto found = std::ranges::find_if(
      diagnostics, [message](const Diagnostic& diagnostic) { return diagnostic.message == message; });
  if (found == diagnostics.end()) {
    throw std::runtime_error("expected diagnostic was not found: " + std::string(message));
  }
  return *found;
}

void expectDiagnosticRange(const std::vector<Diagnostic>& diagnostics, std::string_view message,
                           SourceRange expectedRange) {
  const auto& diagnostic = diagnosticWithMessage(diagnostics, message);
  expect(diagnostic.range == expectedRange, "diagnostic should preserve the expected source range");
}

}  // namespace

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/DiagnosticStore.h"

#include "value/session/SourceIdSet.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace vgmtrans::core {

void DiagnosticStore::addError(std::string message, std::optional<SourceRange> range) {
  diagnostics_.push_back(Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
      .range = range,
  });
}

void DiagnosticStore::append(std::vector<Diagnostic> diagnostics) {
  diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(diagnostics.begin()),
                      std::make_move_iterator(diagnostics.end()));
}

void DiagnosticStore::removeForSources(const std::vector<SourceId>& sources) {
  const auto sourceIds = makeSourceIdSet(sources);
  std::erase_if(diagnostics_, [&](const Diagnostic& diagnostic) {
    return diagnostic.range && sourceIds.contains(diagnostic.range->source);
  });
}

}  // namespace vgmtrans::core

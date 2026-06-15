/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/DiagnosticStore.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::unordered_set<u32> sourceIdSet(const std::vector<SourceId>& sources) {
  std::unordered_set<u32> ids;
  ids.reserve(sources.size());
  for (const SourceId source : sources) {
    ids.insert(source.value);
  }
  return ids;
}

}  // namespace

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
  const auto sourceIds = sourceIdSet(sources);
  std::erase_if(diagnostics_, [&](const Diagnostic& diagnostic) {
    return diagnostic.range && sourceIds.contains(diagnostic.range->source.value);
  });
}

}  // namespace vgmtrans::core

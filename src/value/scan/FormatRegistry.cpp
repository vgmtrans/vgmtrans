/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/FormatRegistry.h"

#include <fmt/format.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

void validateAcceptedFormats(std::string_view processorName, const std::vector<std::string>& formats) {
  std::unordered_set<std::string_view> provided;
  provided.reserve(formats.size());
  for (const auto& format : formats) {
    if (format.empty()) {
      throw std::invalid_argument("Cannot register an empty accepted source format");
    }
    if (!provided.insert(format).second) {
      throw std::invalid_argument(fmt::format("Duplicate source format accepted by {}: {}", processorName, format));
    }
  }
}

}  // namespace

void FormatRegistry::add(FormatDefinition definition) {
  if (sealed_) {
    throw std::logic_error("Cannot register value formats after session mutation has started");
  }
  if (definition.module.name.empty() || !definition.module.scan) {
    throw std::invalid_argument("Cannot register an incomplete FormatModule");
  }

  // Check every ordinary validation failure before changing either index. This
  // prevents a bad definition from leaving the registry partially updated.
  validateAcceptedFormats(definition.module.name, definition.module.acceptedFormats);

  std::unordered_set<std::string_view> providedIds;
  providedIds.reserve(definition.sequenceDialects.size());
  for (const auto& dialect : definition.sequenceDialects) {
    if (!dialect.id.valid()) {
      throw std::invalid_argument("Cannot register a SequenceDialect with an empty id");
    }
    const std::string_view id = dialect.id.value;
    if (!providedIds.insert(id).second || dialects_.contains(dialect.id.value)) {
      throw std::invalid_argument(fmt::format("Duplicate SequenceDialect registered: {}", id));
    }
  }

  modules_.push_back(std::move(definition.module));
  for (auto& dialect : definition.sequenceDialects) {
    const std::string id = dialect.id.value;
    dialects_.emplace(id, std::move(dialect));
  }
}

void FormatRegistry::add(SourceExtractor extractor) {
  if (sealed_) {
    throw std::logic_error("Cannot register source extractors after session mutation has started");
  }
  if (extractor.name.empty() || !extractor.extract) {
    throw std::invalid_argument("Cannot register an incomplete SourceExtractor");
  }
  validateAcceptedFormats(extractor.name, extractor.acceptedFormats);
  extractors_.push_back(std::move(extractor));
}

void FormatRegistry::seal() noexcept {
  sealed_ = true;
}

const FormatModule* FormatRegistry::findModule(std::string_view name) const {
  const auto found = std::ranges::find(modules_, name, &FormatModule::name);
  return found != modules_.end() ? &*found : nullptr;
}

const SequenceDialect* FormatRegistry::findDialect(std::string_view id) const {
  const auto found = dialects_.find(std::string(id));
  return found != dialects_.end() ? &found->second : nullptr;
}

bool FormatRegistry::containsDialect(std::string_view id) const {
  return findDialect(id) != nullptr;
}

}  // namespace vgmtrans::core

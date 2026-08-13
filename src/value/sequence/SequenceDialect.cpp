/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceDialect.h"

#include <fmt/format.h>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

void SequenceDialectRegistry::add(SequenceDialect dialect) {
  if (sealed_) {
    throw std::logic_error("Cannot register sequence dialects after session mutation has started");
  }
  if (!dialect.id.valid()) {
    throw std::invalid_argument("Cannot register a SequenceDialect with an empty id");
  }

  const auto id = dialect.id.value;
  if (!dialects_.emplace(id, std::move(dialect)).second) {
    throw std::invalid_argument(fmt::format("Duplicate SequenceDialect registered: {}", id));
  }
}

void SequenceDialectRegistry::seal() noexcept {
  sealed_ = true;
}

const SequenceDialect* SequenceDialectRegistry::find(std::string_view id) const {
  const auto found = dialects_.find(std::string(id));
  if (found == dialects_.end()) {
    return nullptr;
  }
  return &found->second;
}

bool SequenceDialectRegistry::contains(std::string_view id) const {
  return find(id) != nullptr;
}

}  // namespace vgmtrans::core

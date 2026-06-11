/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/FormatRegistry.h"

#include "value/core/FormatModule.h"

#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

FormatRegistry::FormatRegistry() = default;

FormatRegistry::~FormatRegistry() = default;

FormatRegistry::FormatRegistry(FormatRegistry&&) noexcept = default;

FormatRegistry& FormatRegistry::operator=(FormatRegistry&&) noexcept = default;

void FormatRegistry::add(std::unique_ptr<FormatModule> module) {
  if (!module) {
    throw std::invalid_argument("Cannot register a null FormatModule");
  }
  modules_.push_back(std::move(module));
}

}  // namespace vgmtrans::core

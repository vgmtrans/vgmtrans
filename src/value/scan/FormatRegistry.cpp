/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/FormatRegistry.h"

#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

void FormatRegistry::add(FormatModule module) {
  if (sealed_) {
    throw std::logic_error("Cannot register value formats after session mutation has started");
  }
  if (module.name.empty() || module.canScan == nullptr || module.scan == nullptr) {
    throw std::invalid_argument("Cannot register an incomplete FormatModule");
  }
  modules_.push_back(std::move(module));
}

void FormatRegistry::seal() noexcept {
  sealed_ = true;
}

}  // namespace vgmtrans::core

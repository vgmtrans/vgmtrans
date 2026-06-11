/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/FormatRegistry.h"

#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

void FormatRegistry::add(FormatModule module) {
  if (module.name.empty() || module.canScan == nullptr || module.scan == nullptr) {
    throw std::invalid_argument("Cannot register an incomplete FormatModule");
  }
  modules_.push_back(std::move(module));
}

}  // namespace vgmtrans::core

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/FormatModule.h"

#include <vector>

namespace vgmtrans::core {

class FormatRegistry {
 public:
  void add(FormatModule module);

  [[nodiscard]] const std::vector<FormatModule>& modules() const noexcept {
    return modules_;
  }

 private:
  std::vector<FormatModule> modules_;
};

}  // namespace vgmtrans::core

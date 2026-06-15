/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <vector>

namespace vgmtrans::core {

class FormatRegistry {
public:
  // Order matters: every source is offered to modules in registration order, including
  // derived sources extracted by earlier modules.
  void add(FormatModule module);
  void seal() noexcept;

  [[nodiscard]] const std::vector<FormatModule>& modules() const noexcept { return modules_; }
  [[nodiscard]] bool sealed() const noexcept { return sealed_; }

private:
  std::vector<FormatModule> modules_;
  bool sealed_ = false;
};

}  // namespace vgmtrans::core

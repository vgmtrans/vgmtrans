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
  // Registry order matters: scanners are asked in registration order for every source,
  // including virtual sources extracted by earlier scanners.
  void add(FormatModule module);

  [[nodiscard]] const std::vector<FormatModule>& modules() const noexcept { return modules_; }

private:
  std::vector<FormatModule> modules_;
};

}  // namespace vgmtrans::core

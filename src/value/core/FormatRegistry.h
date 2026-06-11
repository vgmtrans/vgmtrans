/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <memory>
#include <vector>

namespace vgmtrans::core {

class FormatModule;

class FormatRegistry {
 public:
  FormatRegistry();
  ~FormatRegistry();

  FormatRegistry(FormatRegistry&&) noexcept;
  FormatRegistry& operator=(FormatRegistry&&) noexcept;
  FormatRegistry(const FormatRegistry&) = delete;
  FormatRegistry& operator=(const FormatRegistry&) = delete;

  void add(std::unique_ptr<FormatModule> module);

  [[nodiscard]] const std::vector<std::unique_ptr<FormatModule>>& modules() const noexcept {
    return modules_;
  }

 private:
  std::vector<std::unique_ptr<FormatModule>> modules_;
};

}  // namespace vgmtrans::core

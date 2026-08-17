/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"
#include "value/scan/SourceExtractor.h"

#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

class FormatRegistry {
public:
  void add(FormatModule module);
  void add(SourceExtractor extractor);
  void seal() noexcept;

  [[nodiscard]] const std::vector<SourceExtractor>& extractors() const noexcept { return extractors_; }
  [[nodiscard]] const std::vector<FormatModule>& modules() const noexcept { return modules_; }
  [[nodiscard]] const FormatModule* findModule(std::string_view name) const;
  [[nodiscard]] CollectionBinder collectionBinder(std::string_view resolver) const;
  [[nodiscard]] bool sealed() const noexcept { return sealed_; }

private:
  std::vector<SourceExtractor> extractors_;
  std::vector<FormatModule> modules_;
  bool sealed_ = false;
};

}  // namespace vgmtrans::core

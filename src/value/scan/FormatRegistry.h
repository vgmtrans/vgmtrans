/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatDefinition.h"
#include "value/scan/SourceExtractor.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

class FormatRegistry {
public:
  // Registration is validated as one unit before either the ordered module
  // list or the global dialect index changes.
  void add(FormatDefinition definition);
  void add(SourceExtractor extractor);
  void seal() noexcept;

  [[nodiscard]] const std::vector<SourceExtractor>& extractors() const noexcept { return extractors_; }
  [[nodiscard]] const std::vector<FormatModule>& modules() const noexcept { return modules_; }
  [[nodiscard]] const FormatModule* findModule(std::string_view name) const;
  [[nodiscard]] const SequenceDialect* findDialect(std::string_view id) const;
  [[nodiscard]] bool containsDialect(std::string_view id) const;
  [[nodiscard]] bool sealed() const noexcept { return sealed_; }

private:
  std::vector<SourceExtractor> extractors_;
  std::vector<FormatModule> modules_;
  std::unordered_map<std::string, SequenceDialect> dialects_;
  bool sealed_ = false;
};

}  // namespace vgmtrans::core

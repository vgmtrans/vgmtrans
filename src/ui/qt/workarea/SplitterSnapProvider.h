/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <vector>

struct SplitterSnapRange {
  int lowerBound = 0;
  int upperBound = 0;
};

class SplitterSnapProvider {
public:
  virtual ~SplitterSnapProvider() = default;
  [[nodiscard]] virtual std::vector<SplitterSnapRange> splitterSnapRanges() const = 0;
};

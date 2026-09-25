/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/synth/SampleFiltering.h"

#include <functional>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct FormatModule {
  // Function table registered by one format. Recognition belongs at the start
  // of scan(), which returns an empty result when the source does not match.
  // Different modules may scan the same immutable input concurrently.
  using Scan = std::function<ScanResult(const ScanInput& input)>;

  std::string name;
  // Used when a request delegates sample filtering to the owning format.
  SampleFilter preferredSampleFilter = SampleFilter::None;
  // Known source representations accepted by this module. Sources without a
  // known format are still offered to every module for normal discovery.
  std::vector<std::string> acceptedFormats;
  Scan scan;
};

}  // namespace vgmtrans::core

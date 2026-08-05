/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

namespace vgmtrans::core {

enum class SampleFilter {
  None,
  SnesDspLowPass,
};

enum class SampleFilteringPolicy {
  FormatPreferred,
  None,
  SnesDspLowPass,
};

[[nodiscard]] constexpr SampleFilter resolveSampleFilter(SampleFilteringPolicy policy,
                                                         SampleFilter formatPreferred) noexcept {
  switch (policy) {
    case SampleFilteringPolicy::FormatPreferred:
      return formatPreferred;
    case SampleFilteringPolicy::None:
      return SampleFilter::None;
    case SampleFilteringPolicy::SnesDspLowPass:
      return SampleFilter::SnesDspLowPass;
  }
  return SampleFilter::None;
}

}  // namespace vgmtrans::core

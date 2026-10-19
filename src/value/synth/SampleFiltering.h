/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

namespace vgmtrans::core {

struct DecodedSample;

// A concrete processor selected either directly or by a format.
enum class SampleFilter {
  None,
  SnesDspLowPass,
  PsxSpuLowPass,
};

// User-facing policy; FormatPreferred resolves to a SampleFilter per collection.
enum class SampleFilteringPolicy {
  FormatPreferred,
  None,
  SnesDspLowPass,
  PsxSpuLowPass,
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
    case SampleFilteringPolicy::PsxSpuLowPass:
      return SampleFilter::PsxSpuLowPass;
  }
  return SampleFilter::None;
}

void applySampleFilter(DecodedSample& sample, SampleFilter filter);

}  // namespace vgmtrans::core

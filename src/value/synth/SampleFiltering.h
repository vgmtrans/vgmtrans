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

void applySampleFilter(DecodedSample& sample, SampleFilter filter);

}  // namespace vgmtrans::core

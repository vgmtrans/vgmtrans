/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/SourceExtractor.h"

namespace vgmtrans::formats::psf {

inline constexpr char kPsf2IniAttribute[] = "psf2.ini";

[[nodiscard]] core::SourceExtractor psfExtractor();

}  // namespace vgmtrans::formats::psf

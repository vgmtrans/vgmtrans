/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <vector>

namespace vgmtrans::formats::akao {

[[nodiscard]] std::vector<core::DesiredCollection> resolveAkaoCollections(const core::MatchContext& context);

}  // namespace vgmtrans::formats::akao

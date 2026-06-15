/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <string_view>
#include <vector>

namespace vgmtrans::core {

// Common resolver for formats that already know an asset belongs to a discovered
// collection while scanning. More complex formats can still provide their own
// resolver over IDs, filenames, sample coverage, or format-specific facts.
[[nodiscard]] std::vector<DesiredCollection> resolveCollectionMemberFacts(const MatchContext& context,
                                                                          std::string_view resolver,
                                                                          std::string_view format = {});

}  // namespace vgmtrans::core

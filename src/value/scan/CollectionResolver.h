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

// Resolver for the common case where scanners emit CollectionMemberFact records.
// Formats with looser matching rules can provide their own resolver instead.
[[nodiscard]] std::vector<DesiredCollection> resolveCollectionMemberFacts(const MatchContext& context,
                                                                          std::string_view resolver,
                                                                          std::string_view format = {});

}  // namespace vgmtrans::core
